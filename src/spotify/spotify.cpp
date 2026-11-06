/**
 * @file
 * @brief Spotify control server requests and reply wording.
 */

#include "spotify.hpp"

#include <algorithm>
#include <stdexcept>

using std::runtime_error;
using std::size_t;
using std::to_string;

namespace {
/**
 * @brief "Title by Artist" for a Spotify track object.
 */
string describe(const json& track) {
    const auto& artists = track.value("artists", json::array());
    const string artist = !artists.empty() && artists[0].is_object() ? artists[0].value("name", "") : "";
    return track.value("name", "an unknown track") + (artist.empty() ? "" : " by " + artist);
}

/**
 * @brief A duration such as "3 minutes 20 seconds".
 */
string duration(long long ms) {
    const auto minutes = ms / 60000;
    const auto seconds = ms / 1000 % 60;
    const auto plural = [](long long count, const string& unit) { return to_string(count) + " " + unit + (count == 1 ? "" : "s"); };

    return minutes == 0 ? plural(seconds, "second") : plural(minutes, "minute") + (seconds ? " " + plural(seconds, "second") : "");
}

/**
 * @brief Why a server response failed, or "" when it succeeded.
 */
string failure(const httplib::Result& response) {
    if(!response) {
        return "Cannot reach the Spotify server: " + httplib::to_string(response.error());
    } else if(response->status < 200 || response->status >= 300) {
        return "Spotify server returned HTTP " + to_string(response->status) + (response->body.empty() ? "" : ": " + response->body);
    }

    return "";
}

/**
 * @brief Decode a response body, treating an empty one as null.
 */
json decode(const httplib::Result& response) {
    const auto problem = failure(response);

    if(!problem.empty()) {
        throw runtime_error(problem);
    } else if(response->body.empty()) {
        return nullptr;
    }

    const auto body = json::parse(response->body, nullptr, false);

    if(body.is_discarded()) {
        throw runtime_error("Spotify server returned invalid JSON");
    }

    return body;
}
} // namespace

Spotify::Spotify(const string& url) : client(url.c_str()) {
    client.set_connection_timeout(2);
    client.set_read_timeout(10);
}

json Spotify::get(const string& path, const httplib::Params& params) {
    return decode(client.Get(path, params, httplib::Headers{}));
}

json Spotify::send(const string& method, const string& path, const json& body) {
    const auto text = body.dump();
    return decode(method == "PUT" ? client.Put(path, text, "application/json") : client.Post(path, text, "application/json"));
}

Devices Spotify::discoverDevices() {
    Devices devices;

    for(const auto& device : get("/devices")) {
        if(device.is_object() && device.value("id", json()).is_string() && !device.value("is_restricted", false)) {
            const string state = device.value("is_active", false) ? "active" : "idle";
            devices.push_back({"spotify." + device["id"].get<string>(), device.value("name", device["id"].get<string>()), "spotify", state, device});
        }
    }

    return devices;
}

ServiceCatalogue Spotify::discoverServices() {
    const json text = {{"text", json::object()}};
    const ServiceFields song{{"song"}, {"song"}, {{"song", text}}, false};

    return {{"spotify", {{"cast", {}}, {"get_queue", {{}, {}, {}, false}}, {"request_song", song}, {"time_until", song}}}};
}

Result Spotify::preview(const Command& command) {
    Result result{false, "", command};
    const auto services = discoverServices();
    const auto* fields = findService(services, command.domain, command.service);

    if(!fields) {
        result.error = "Service is unavailable: " + command.domain + "." + command.service;
        return result;
    } else if(!command.data.is_object()) {
        result.error = "Command data must be an object";
        return result;
    }

    for(const auto& [field, value] : command.data.items()) {
        if(!fields->allowed.count(field)) {
            result.error = "Unsupported field for " + command.service + ": " + field;
            return result;
        } else if(!value.is_string() || value.get<string>().empty()) {
            result.error = field + " must be non-empty text";
            return result;
        }
    }

    for(const auto& field : fields->required) {
        if(!command.data.contains(field)) {
            result.error = "Missing required field for " + command.service + ": " + field;
            return result;
        }
    }

    if(!fields->targetsEntity) {
        result.command.entity = "";
        result.success = true;
        return result;
    }

    try {
        const auto devices = discoverDevices();
        const bool found = std::any_of(devices.begin(), devices.end(), [&](const Device& device) { return device.entityId == command.entity; });
        result.success = found;
        result.error = found ? "" : "Spotify device is unavailable: " + command.entity;
    } catch(const std::exception& error) {
        result.error = error.what();
    }

    return result;
}

Result Spotify::execute(const Command& command) {
    auto result = preview(command);

    if(!result.success) {
        return result;
    }

    try {
        if(command.service == "cast") {
            send("PUT", "/transfer", {{"device_id", command.entity.substr(command.entity.find('.') + 1)}, {"play", true}});
        } else if(command.service == "get_queue") {
            const auto queue = get("/queue");
            const auto& playing = queue.value("currently_playing", json());
            const auto& next = queue.value("queue", json::array());
            string reply = playing.is_object() ? "Playing " + describe(playing) + "." : "Nothing is playing.";

            // A spoken answer stays short: name only the next few tracks.
            for(size_t i = 0; i < next.size() && i < 3; ++i) {
                reply += (i == 0 ? " Next: " : ", ") + describe(next[i]);
            }

            result.reply = next.empty() ? reply + " The queue is empty." : reply + ".";
        } else if(command.service == "request_song") {
            // The server finds the best match for the description and queues it.
            const auto queued = send("POST", "/request", {{"song", command.data["song"]}});
            result.reply = "Queued " + describe(queued.value("track", json::object())) + ".";
        } else {
            // The server matches the description against the queue; 404 means it is not queued.
            const auto response = client.Get("/time-until", {{"song", command.data["song"].get<string>()}}, httplib::Headers{});

            if(response && response->status == 404) {
                result.reply = "\"" + command.data["song"].get<string>() + "\" is not in the queue.";
            } else {
                const auto wait = decode(response);
                const auto ms = wait.value("ms", json());

                if(!ms.is_number()) {
                    throw runtime_error("Spotify server returned no time");
                }

                const auto track = describe(wait.value("track", json::object()));
                result.reply = ms.get<long long>() <= 0 ? track + " is playing now." : track + " plays in " + duration(ms.get<long long>()) + ".";
            }
        }
    } catch(const std::exception& error) {
        result.success = false;
        result.error = error.what();
    }

    return result;
}