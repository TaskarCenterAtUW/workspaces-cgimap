#ifndef WORKSPACES_TENANT_HPP
#define WORKSPACES_TENANT_HPP

#include "cgimap/request.hpp"
#include "cgimap/workspaces/types.hpp"

namespace workspaces {
  std::optional<workspace_id_t> try_id_from_request(const request& req);
}

#endif /* WORKSPACES_TENANT_HPP */
