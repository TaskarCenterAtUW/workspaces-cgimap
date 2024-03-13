#ifndef WORKSPACES_TDEI_AUTH_HPP
#define WORKSPACES_TDEI_AUTH_HPP

#include "cgimap/data_selection.hpp"
#include "cgimap/data_update.hpp"
#include "cgimap/request.hpp"

namespace workspaces {
  std::optional<osm_user_id_t> authenticate_user(
    const request& req,
    data_selection& selection,
    data_update::factory& update_factory);
}

#endif /* WORKSPACES_TDEI_AUTH_HPP */
