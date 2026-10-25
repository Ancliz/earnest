#pragma once

#include "parser.hpp"

// Replace aliases using media-player states; preserve the other parser options
// Throws for invalid state data or a response containing no media players
void populateDeviceAliases(const Parser::json& states, Parser::Options& options);
