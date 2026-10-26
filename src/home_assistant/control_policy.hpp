#pragma once

#include "parser/parser.hpp"

// Empty means permitted. Uses freshly fetched states immediately before posting.
std::string validateCommand(const Parser::json& request, const Parser::json& states,
                            const Parser::json& configuration);
void validateControlConfiguration(const Parser::json& configuration);