/**
 * @file
 * @brief YAML conversion, bounded file access, and configuration reloads.
 */

#include "configuration.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <yaml.h>

using std::runtime_error;
using std::size_t;
using std::string;

namespace Config {
namespace {

/// Largest accepted configuration, in bytes.
constexpr size_t maxBytes = 256 * 1024;

/// Deepest accepted nesting, which also bounds alias cycles.
constexpr unsigned maxDepth = 16;

/// Most nodes visited after alias expansion.
constexpr unsigned maxNodes = 32768;

/**
 * @brief Owns a libyaml parser reading from a string.
 */
struct YamlParser {

    /// libyaml parser state.
    yaml_parser_t handle;

    /**
     * @brief Start parsing text, which must outlive the parser.
     * @throws std::runtime_error If libyaml cannot initialize.
     */
    explicit YamlParser(const string& text) {
        if(!yaml_parser_initialize(&handle)) {
            throw runtime_error("Cannot initialize YAML parser");
        }

        yaml_parser_set_input_string(&handle, reinterpret_cast<const unsigned char*>(text.data()), text.size());
    }

    /**
     * @brief Release the parser.
     */
    ~YamlParser() {
        yaml_parser_delete(&handle);
    }
};

/**
 * @brief Owns the next document loaded from a parser.
 */
struct YamlDocument {
    
    /// libyaml document tree.
    yaml_document_t handle;

    /**
     * @brief Load the parser's next document; an exhausted stream yields an empty document.
     * @throws std::runtime_error With the line and problem when the YAML is invalid.
     */
    explicit YamlDocument(yaml_parser_t& parser) {
        if(!yaml_parser_load(&parser, &handle)) {
            throw runtime_error("YAML line " + std::to_string(parser.problem_mark.line + 1) + ": " +
                                (parser.problem ? parser.problem : "invalid YAML"));
        }
    }

    /**
     * @brief Release the document.
     */
    ~YamlDocument() {
        yaml_document_delete(&handle);
    }
};

/**
 * @brief Read a whole file of 1..maxBytes bytes.
 * @throws std::runtime_error If the file cannot be read, is out of bounds, or changes length while read.
 */
string readFile(const string& path) {
    std::ifstream stream(path);

    if(!stream) {
        throw runtime_error("Cannot read configuration file: " + path);
    }

    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();

    if(size < 1 || static_cast<size_t>(size) > maxBytes) {
        throw runtime_error("Configuration YAML must contain 1..262144 bytes");
    }

    stream.seekg(0);
    string text(static_cast<size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));

    if(!stream || stream.peek() != std::char_traits<char>::eof()) {
        throw runtime_error("Configuration file changed while reading");
    }

    return text;
}

/**
 * @brief Text of a scalar node.
 */
string scalarText(const yaml_node_t& node) {
    return string(reinterpret_cast<const char*>(node.data.scalar.value), node.data.scalar.length);
}

/**
 * @brief Convert a document node to JSON, bounding depth and alias expansion.
 * @param[in] depth Nesting of this node; the root is zero.
 * @param[in,out] budget Nodes still allowed; every visit, including through an alias, uses one.
 * @throws std::runtime_error For duplicate or non-scalar keys and exceeded limits.
 */
json toJson(yaml_document_t& document, yaml_node_t* node, unsigned depth, unsigned& budget) {
    if(budget == 0) {
        throw runtime_error("YAML expansion exceeds " + std::to_string(maxNodes) + " nodes");
    }

    --budget;

    if(!node || depth > maxDepth) {
        throw runtime_error("YAML nesting or alias cycle exceeds " + std::to_string(maxDepth) + " levels");
    }

    if(node->type == YAML_SEQUENCE_NODE) {
        json list = json::array();

        for(auto item = node->data.sequence.items.start; item != node->data.sequence.items.top; ++item) {
            list.push_back(toJson(document, yaml_document_get_node(&document, *item), depth + 1, budget));
        }

        return list;
    }

    if(node->type == YAML_MAPPING_NODE) {
        json map = json::object();

        for(auto pair = node->data.mapping.pairs.start; pair != node->data.mapping.pairs.top; ++pair) {
            const auto* key = yaml_document_get_node(&document, pair->key);

            if(!key || key->type != YAML_SCALAR_NODE) {
                throw runtime_error("YAML keys must be strings");
            }

            const string name = scalarText(*key);

            if(map.contains(name)) {
                throw runtime_error("Duplicate YAML key: " + name);
            }

            map[name] = toJson(document, yaml_document_get_node(&document, pair->value), depth + 1, budget);
        }

        return map;
    }

    if(node->type != YAML_SCALAR_NODE) {
        throw runtime_error("Unsupported YAML node");
    }

    const string value = scalarText(*node);

    // Only plain (unquoted) scalars can be booleans, null or numbers.
    if(node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE) {
        if(value == "true" || value == "false") {
            return value == "true";
        }

        if(value == "null" || value == "~" || value.empty()) {
            return nullptr;
        }

        const auto number = json::parse(value, nullptr, false);

        if(number.is_number() && std::isfinite(number.get<double>())) {
            return number;
        }
    }

    return value;
}
} // namespace

json readYaml(const string& text) {
    if(text.empty() || text.size() > maxBytes) {
        throw runtime_error("Phrase YAML must contain 1..262144 bytes");
    }

    YamlParser parser(text);
    YamlDocument document(parser.handle);
    unsigned budget = maxNodes;
    json result = toJson(document.handle, yaml_document_get_root_node(&document.handle), 0, budget);
    YamlDocument trailing(parser.handle);

    if(yaml_document_get_root_node(&trailing.handle)) {
        throw runtime_error("Use one YAML document per phrase file");
    }

    return result;
}

json readYamlFile(const string& path) {
    return readYaml(readFile(path));
}

json mergeConfiguration(json base, const json& overlay) {
    if(base.is_object() && overlay.is_object()) {
        for(const auto& [key, value] : overlay.items()) {
            base[key] = base.contains(key) ? mergeConfiguration(base[key], value) : value;
        }
    } else if(base.is_array() && overlay.is_array()) {
        base.insert(base.end(), overlay.begin(), overlay.end());
    } else {
        base = overlay;
    }

    return base;
}

Configuration::Configuration(string path, std::function<void(const json&)> apply, string homePath)
    : path(std::move(path)), homePath(std::move(homePath)), apply(std::move(apply)) {
    lastText = readTexts();
    load(lastText);
}

string Configuration::readTexts() const {
    const bool home = !homePath.empty() && std::filesystem::exists(homePath) && std::filesystem::file_size(homePath) > 0;
    return readFile(path) + '\0' + (home ? readFile(homePath) : "");
}

void Configuration::load(const string& text) {
    const auto split = text.find('\0');
    const string home = text.substr(split + 1);
    json candidate = readYaml(text.substr(0, split));

    if(!home.empty()) {
        candidate = mergeConfiguration(std::move(candidate), readYaml(home));
    }


    try {
        apply(candidate);
    } catch(...) {
        // One consumer may have accepted the candidate before another rejected it.
        if(!active.is_null()) {
            apply(active);
        }

        throw;
    }

    active = std::move(candidate);
}

bool Configuration::reloadIfChanged() {
    string text = readTexts();

    if(text == lastText) {
        return false;
    }

    lastText = std::move(text);
    load(lastText);
    return true;
}
} // namespace Config