#include "detail.hpp"

#include <cmath>
#include <stdexcept>
#include <yaml.h>

using nlohmann::json;
using std::runtime_error;
using std::string;

namespace {

json convert(yaml_document_t& document, yaml_node_t* node, unsigned depth) {
    if(!node || depth > 16) {
        throw runtime_error("YAML nesting or alias cycle exceeds 16 levels");
    }

    if(node->type == YAML_SEQUENCE_NODE) {
        json result = json::array();

        for(auto item = node->data.sequence.items.start; item != node->data.sequence.items.top; ++item) {
            result.push_back(convert(document, yaml_document_get_node(&document, *item), depth + 1));
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

            result[name] = convert(document, yaml_document_get_node(&document, pair->value), depth + 1);
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
        result = convert(document, yaml_document_get_root_node(&document), 0);
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