/**
 * @file
 * @brief Authenticated discovery and explicit checked command execution.
 */

#pragma once

#include "commands/command.hpp"
#include <httplib.h>
#include <string>

class HomeAssistant final : public Commands::Executor {
    httplib::Client client;

public:
    /**
     * @brief Configure an authenticated HTTP client without sending a request.
     *
     * Installs the bearer header and connection/read/write timeouts of 5/10/10 seconds.
     * Discovery and control remain explicit operations after construction.
     *
     * @param[in] url Home Assistant origin, including scheme and optional port.
     * @param[in] token Nonempty long-lived access token; copied into the client header.
     * @throws std::runtime_error If token is empty.
     * @note Configures local client state only; does not discover or control devices.
     */
    explicit HomeAssistant(const std::string& url, const std::string& token);
    /**
     * @brief Fetch the current Home Assistant entity-state snapshot.
     *
     * Performs one authenticated GET to /api/states and requires HTTP 200 plus a JSON
     * array response. Individual entity records are interpreted by separate discovery
     * and validation functions.
     *
     * @return Independently owned state array.
     * @throws std::runtime_error For transport failure, non-200 status, or a response
     *                            that is not a JSON array.
     * @note Performs read-only HTTP and no application logging or control POST.
     */
    Commands::json getStates();
    /**
     * @brief Fetch the currently advertised Home Assistant services.
     *
     * Performs one authenticated GET to /api/services. Callers inspect domain entries
     * and their services maps; this method verifies only the outer JSON array.
     *
     * @return Independently owned array of advertised service groups.
     * @throws std::runtime_error For failed/non-200 HTTP or invalid outer JSON structure.
     * @note Performs read-only HTTP without logging, caching, or controlling devices.
     */
    Commands::json getServices();
    /**
     * @brief Resolve a command and verify live policy and service availability without posting.
     *
     * Fetches current states, calls prepareCommand(), then verifies that the resolved
     * service is advertised by getServices(). No result from the parser is implicitly
     * trusted, and relative volume is calculated from this freshly fetched snapshot.
     *
     * @param[in] request Proposed service request or internal volume-adjustment intent.
     * @param[in] configuration Active installation/control policy snapshot.
     * @return Success with a concrete service request, or failure with a diagnostic error.
     *         Invalid data and HTTP exceptions are returned as failures.
     * @note Performs read-only HTTP. A successful check neither changes device state nor
     *       guarantees that a later execution sees the same state.
     */
    Commands::ExecutionResult check(const Commands::json& request,
                                    const Commands::json& configuration) override;
    /**
     * @brief Check current conditions and submit at most one Home Assistant control POST.
     *
     * Calls check() immediately before submission and posts its resolved endpoint/body
     * only when validation succeeds. Any 2xx response is accepted; transport errors,
     * non-2xx statuses, and caught exceptions become structured failures.
     *
     * @param[in] request Proposed command; internal intents are resolved during checking.
     * @param[in] configuration Active installation/control policy snapshot.
     * @return Execution result, including the submitted request on success or error text
     *         on failure. A failed transport response cannot prove a command was unapplied.
     * @note Changes external state on successful submission. Does not retry, log, delegate
     *       through Assist, or make checking and posting an atomic transaction.
     */
    Commands::ExecutionResult execute(const Commands::json& request,
                                      const Commands::json& configuration) override;
};
