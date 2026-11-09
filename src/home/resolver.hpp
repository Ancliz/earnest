/**
 * @file
 * @brief The one place that decides which device a spoken name means.
 */

#pragma once

#include "registry.hpp"

#include <set>

namespace Home {

/**
 * @brief How much of a spoken name has to match.
 */
enum class Reach {

    /// The whole name, allowing a small misspelling. For text that may not be a device at all.
    Names,

    /// A whole name exactly, or otherwise as many of the words as possible, however misheard.
    Words
};

/**
 * @brief What a spoken name turned out to mean.
 */
struct Resolution {

    /**
     * @brief Whether a device was found and, if not, why.
     */
    enum class Outcome { Found, Unknown, Ambiguous, OtherDomain };

    /// Whether a device was found and, if not, why.
    Outcome outcome = Outcome::Unknown;

    /// The entity to command when one was found.
    string entity;

    /**
     * @brief Whether a device was found.
     */
    explicit operator bool() const { return outcome == Outcome::Found; }
};

/**
 * @brief Matches spoken names against the devices discovered for one request.
 *
 * Entities of one physical device share all their names, so a name known to one executor reaches
 * the same device's entity in another domain. Devices come from the registry first, then from what
 * executors report; a device with no entity present is ignored.
 */
class Resolver {

    /**
     * @brief One physical device, as far as it is present right now.
     */
    struct Known {

        /// Normalized whole names.
        std::set<string> names;

        /// Every word of its names and of its room's names.
        vector<string> words;

        /// Room it is in, or "".
        string room;

        /// Its present entities by domain, most preferred first.
        std::map<string, vector<string>> entities;

        /**
         * @brief Record a whole name and its words.
         */
        void addName(const string& name);

        /**
         * @brief Record a present entity and every name it goes by.
         */
        void addEntity(const Commands::Device& device, const Registry& registry);
    };

    /// Devices present for this request.
    vector<Known> known;

    /// Entity used when no name is spoken.
    string defaultDevice;

    /// Room the request came from, or "".
    string room;

    /// Whether misspelled names may match.
    bool fuzzy;

    /**
     * @brief Record one physical device made of these discovered entities.
     * @param[in] configuredRoom Room from the registry, which overrides the discovered one.
     */
    void addDevice(const vector<const Commands::Device*>& members, const string& configuredRoom, const Registry& registry);

    /**
     * @brief Devices of the domain with the closest whole name; empty when none is close enough.
     */
    vector<const Known*> closestNames(const string& name, const string& domain, bool allowEdits) const;

    /**
     * @brief Find the device of the domain matching the most words, watching for other domains.
     */
    Resolution matchWords(const vector<string>& heard, const string& domain, const string& preferred) const;

    /**
     * @brief Settle equally good matches using the room and then the preferred entity.
     */
    Resolution choose(vector<const Known*> matches, const string& domain, const string& preferred) const;

public:
    /**
     * @brief Index the devices in a context.
     */
    Resolver(const Registry& registry, const Commands::Context& context);

    /**
     * @brief The context's default device, or the same physical device's entity in this domain, otherwise "".
     */
    string defaultIn(const string& domain) const;

    /**
     * @brief Find the entity of a domain that a spoken name means.
     * @param[in] spoken Name as heard; nothing matches an empty name.
     * @param[in] domain Domain the entity must be in.
     * @param[in] reach How much of the name has to match.
     * @param[in] preferred Entity to keep if it is among equally good matches, such as a model's choice.
     */
    Resolution resolve(const string& spoken, const string& domain, Reach reach = Reach::Names, const string& preferred = "") const;
};
} // namespace Home