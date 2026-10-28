/**
 * @file
 * @brief YAML conversion, bounded file access, and configuration snapshot updates.
 */

#include "configuration.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <yaml.h>

using nlohmann::json;
using std::runtime_error;
using std::string;

namespace Config {

namespace {

/**
 * @brief Read exactly the checked length of a bounded configuration file.
 *
 * @param[in] path Configuration filename to open with an input stream.
 * @return Owned file contents containing 1..262144 bytes.
 * @throws std::runtime_error If opening/seeking/reading fails, the size is outside
 *                            bounds, or the length changes during the read.
 * @note Performs filesystem I/O only. Same-length concurrent writes are not detected
 *       here; decoded content must still pass downstream validation.
 */
string readFile(const string& path) {
    std::ifstream stream(path);

    if(!stream) {
        throw runtime_error("Cannot read configuration file: " + path);
    }

    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();

    if(size < 1 || size > 256 * 1024) {
        throw runtime_error("Configuration YAML must contain 1..262144 bytes");
    }

    stream.seekg(0);
    string text(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));

    if(!stream || stream.peek() != std::char_traits<char>::eof()) {
        throw runtime_error("Configuration file changed while reading");
    }

    return text;
}

/**
 * @brief Recursively convert a libyaml document node while bounding alias expansion.
 *
 * Walks sequences/mappings by document node ID, rejects duplicate/non-scalar keys,
 * and converts selected plain scalars into JSON booleans, nulls, or finite numbers.
 * Quoted scalars remain strings. Every expanded visit consumes one budget unit.
 *
 * @param[in] document Live libyaml document that owns node and referenced aliases.
 * @param[in] node Current node; a null pointer is invalid.
 * @param[in] depth Current recursion depth, starting at zero and limited to 16.
 * @param[in,out] budget Remaining expanded-node budget, decremented on each visit.
 * @return Independently owned JSON representation of the node subtree.
 * @throws std::runtime_error For invalid nodes, duplicate keys, or exhausted bounds.
 * @note Does not delete libyaml resources or perform file I/O; the caller owns cleanup.
 */
json convert(yaml_document_t& document, yaml_node_t* node, unsigned depth, unsigned& budget) {
    if(budget == 0) {
        throw runtime_error("YAML expansion exceeds 32768 nodes");
    }

    --budget;

    if(!node || depth > 16) {
        throw runtime_error("YAML nesting or alias cycle exceeds 16 levels");
    }

    if(node->type == YAML_SEQUENCE_NODE) {
        json result = json::array();

        for(auto item = node->data.sequence.items.start; item != node->data.sequence.items.top; ++item) {
            result.push_back(convert(document, yaml_document_get_node(&document, *item), depth + 1, budget));
        }

        return result;
    }

    if(node->type == YAML_MAPPING_NODE) {
        json result = json::object();

        for(auto pair = node->data.mapping.pairs.start; pair != node->data.mapping.pairs.top; ++pair) {
            const auto* key = yaml_document_get_node(&document, pair->key);

            if(!key || key->type != YAML_SCALAR_NODE) {
                throw runtime_error("YAML keys must be strings");
            }

            const string name(reinterpret_cast<char*>(key->data.scalar.value), key->data.scalar.length);

            if(result.contains(name)) {
                throw runtime_error("Duplicate YAML key: " + name);
            }

            result[name] =
                convert(document, yaml_document_get_node(&document, pair->value), depth + 1, budget);
        }

        return result;
    }

    if(node->type != YAML_SCALAR_NODE) {
        throw runtime_error("Unsupported YAML node");
    }

    const string value(reinterpret_cast<char*>(node->data.scalar.value), node->data.scalar.length);

    if(node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE) {
        if(value == "true") {
            return true;
        }

        if(value == "false") {
            return false;
        }

        if(value == "null" || value == "~" || value.empty()) {
            return nullptr;
        }

        auto numeric = json::parse(value, nullptr, false);

        if(numeric.is_number() && std::isfinite(numeric.get<double>())) {
            return numeric;
        }
    }

    return value;
}

} // namespace

/**
 * @copydoc Config::readYaml(const std::string&)
 */
json readYaml(const string& text) {
    if(text.empty() || text.size() > 256 * 1024) {
        throw runtime_error("Phrase YAML must contain 1..262144 bytes");
    }

    yaml_parser_t parser;

    if(!yaml_parser_initialize(&parser)) {
        throw runtime_error("Cannot initialize YAML parser");
    }

    yaml_parser_set_input_string(&parser, reinterpret_cast<const unsigned char*>(text.data()), text.size());
    yaml_document_t document;

    if(!yaml_parser_load(&parser, &document)) {
        const string problem = parser.problem ? parser.problem : "invalid YAML";
        const auto line = parser.problem_mark.line + 1;
        yaml_parser_delete(&parser);
        throw runtime_error("YAML line " + std::to_string(line) + ": " + problem);
    }

    json result;
    bool firstDocument = true;

    try {
        unsigned budget = 32768;
        result = convert(document, yaml_document_get_root_node(&document), 0, budget);
        yaml_document_delete(&document);
        firstDocument = false;
        yaml_document_t extra;

        if(!yaml_parser_load(&parser, &extra)) {
            throw runtime_error("Invalid trailing YAML document");
        }

        const bool multiple = yaml_document_get_root_node(&extra) != nullptr;
        yaml_document_delete(&extra);

        if(multiple) {
            throw runtime_error("Use one YAML document per phrase file");
        }
    } catch(...) {
        if(firstDocument) {
            yaml_document_delete(&document);
        }

        yaml_parser_delete(&parser);
        throw;
    }

    yaml_parser_delete(&parser);
    return result;
}

/**
 * @copydoc Config::readYamlFile(const std::string&)
 */
json readYamlFile(const string& path) {
    return readYaml(readFile(path));
}

string defaultFile() {
#ifdef SMARTHOME_PHRASE_FILE
    return SMARTHOME_PHRASE_FILE;
#else
    return "config/phrases.yaml";
#endif
}

/** @cond OUT_OF_LINE_CONSTRUCTOR */
Configuration::Configuration(string path, std::function<void(const json&)> apply)
    : path(std::move(path)), apply(std::move(apply)) {
    reload();
}

/** @endcond */

void Configuration::load(string text) {
    auto next = readYaml(text);
    string nextSeen = text;
    apply(next);
    values.swap(next);
    active.swap(text);
    seen.swap(nextSeen);
    failure.clear();
}

void Configuration::reload() {
    load(readFile(path));
}

bool Configuration::reloadIfChanged() {
    try {
        string text = readFile(path);

        if(text == seen) {
            if(text == active) {
                failure.clear();
            }

            return false;
        }

        seen = text;
        load(std::move(text));
        return true;
    } catch(const std::exception& error) {
        failure = error.what();
        return false;
    }
}

const json& Configuration::data() const {
    return values;
}

const string& Configuration::error() const {
    return failure;
}
} // namespace Config
