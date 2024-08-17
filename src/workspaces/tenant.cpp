#include "cgimap/logger.hpp"
#include "cgimap/workspaces/tenant.hpp"

#include <charconv>
#include <fmt/core.h>

namespace {

std::optional<workspace_id_t> try_s_to_workspace_id(const std::string_view& id)
{
  workspace_id_t out;

  const auto [_, ec] = std::from_chars(id.data(), id.data() + id.size(), out);

  if (ec != std::errc()) {
    logger::message("Failed to parse workspace ID from header");
    return std::nullopt;
  }

  return out;
}

std::optional<int> try_id_from_request_header(const request& req)
{
  const char* const workspace_header = req.get_param("HTTP_X_WORKSPACE");

  if (workspace_header == nullptr) {
    logger::message("No workspace header");
    return std::nullopt;
  }

  const std::string_view header_sv(workspace_header);

  if (header_sv.find_first_not_of("0123456789") != std::string::npos) {
    logger::message(fmt::format("Invalid workspace header: {}", header_sv));
    return std::nullopt;
  }

  return try_s_to_workspace_id(header_sv);
}

} // anonymous namespace


std::optional<workspace_id_t> workspaces::try_id_from_request(const request& req)
{
  const auto id_opt = try_id_from_request_header(req);

  if (id_opt) {
    logger::message(fmt::format("Selected workspace {} from header", *id_opt));
    return id_opt;
  }

  // TODO: support workspace ID cookie?

  return std::nullopt;
}
