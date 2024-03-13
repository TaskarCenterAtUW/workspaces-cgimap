#include "cgimap/logger.hpp"
#include "cgimap/workspaces/tdei_auth.hpp"

#include <cstdlib>
#include <fmt/core.h>
#include <jwt-cpp/jwt.h>

namespace {

// The JWT-CPP library supports several JSON libraries. This one comes bundled:
using json_traits = jwt::traits::kazuho_picojson;

// TODO: make the issuer configurable as a program option:
const char* const JWT_ISSUER = std::getenv("TDEI_JWT_ISSUER");

// TODO: download this from the issuer:
const char* const JWT_ISSUER_X5C_DER = std::getenv("TDEI_JWT_ISSUER_X5C");
//const auto JWT_ISSUER_X5C_PEM = jwt::helper::convert_base64_der_to_pem(JWT_ISSUER_X5C_DER);
const auto JWT_ISSUER_X5C_PEM = jwt::helper::convert_base64_der_to_pem(
  "MIIClzCCAX8CBgGDWU2dszANBgkqhkiG9w0BAQsFADAPMQ0wCwYDVQQDDAR0ZGVpMB4XDTIyMDkyMDA1MDgyMloXDTMyMDkyMDA1MTAwMlowDzENMAsGA1UEAwwEdGRlaTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMLmXLD301sqTGdl/3JSWFu5HmUj6lLzCGjCg75U0vd/xIbWvzjwdM6dq5+7/cxdXPzal5pVHVVd3GS3j28BL52L9Ig+GIy9IhYMSUESjaLxKi8VkVMVD4mpskfTtwH7t4x1ktxEjM0J6Fl/z0wPaMkvJ1U0fX0xx2giz0cdsLNdL0emubXaA6fNZE5OadkNaox2NHCYfHq4XVwymOjK+6qrf26mR4SKnSI2LmqfPyuk1T8kf5g0g2bO4OWKF/KtQbWDKan5LrgHaQUDApNAAqJM3pwJev8DRMxwhUo0vid78X08OR5cYer4Agqq6rub3+apmN4tPfLpzTNeFU/u45UCAwEAATANBgkqhkiG9w0BAQsFAAOCAQEAQewqJdRrONiLc5+yPlEM3l08lOP7PQWLiIoIWPFT4Ots/gXX5pcvoqwJrrn9enMWNkaFiTURscavsuao4uOROi3ZKd21F/cEQHH9wAzTXHJ3cVpTYeyD+UQNgbzcuXMUA3S6cGOJYufB5nrFwSU0GQkuhsuQCHdAuCTVcyv2fDXHNy/D355JpcODV27Lv4wNzpDkoTTBZf07rFkMgYLiAhj38i6KgWfV0MVVmW5B5AULkpOAgcwihEuF0gcPDrV0eGqczQblwUwKxiRc1qcm1D7VLVzPHT8QBd20sXpBtzR1Ic7bXLG6TMCsM8BtY6ezYpf8CS3/u5ANoMVl2SY9hw==");

std::optional<std::string> try_token_from_request(const request& req)
{
  const char* const auth_hdr = req.get_param("HTTP_AUTHORIZATION");

  if (auth_hdr == nullptr) {
    logger::message("No auth header for TDEI token");
    return std::nullopt;
  }

  const std::string_view header_sv(auth_hdr);

  if (!header_sv.rfind("Bearer ", 0) == 0) {
    logger::message("Non-bearer auth header is not a TDEI token");
    return std::nullopt;
  }

  return std::string(header_sv.substr(7));
}

std::optional<jwt::decoded_jwt<json_traits>> try_decode_token(const std::string& encoded)
{
  try {
    return jwt::decode(encoded);
  } catch (const std::exception& ex) {
    logger::message(fmt::format("Failed to decode TDEI auth token: {}", ex.what()));
    return std::nullopt;
  }

  return std::nullopt;
}

bool verify_token(const jwt::decoded_jwt<json_traits>& decoded_token)
{
  const auto verifier = jwt::verify()
    .allow_algorithm(jwt::algorithm::rs256(JWT_ISSUER_X5C_PEM, "", "", ""))
    .with_issuer(JWT_ISSUER);

  try {
    verifier.verify(decoded_token);
  } catch (...) {
    return false;
  }

  return true;
}

std::optional<std::string> try_string_claim(
  const jwt::decoded_jwt<json_traits>& decoded_token,
  const std::string& claim_name
) {
  try {
    const auto claim = decoded_token.get_payload_claim(claim_name);
    return claim.as_string();
  } catch (...) {
    return std::nullopt;
  }
}

osm_user_id_t provision_user(
  data_update::factory& update_factory,
  const std::string& subject,
  const jwt::decoded_jwt<json_traits>& decoded_token
) {
  logger::message(fmt::format("Provisioning TDEI user {}...", subject));

  const auto email_opt = try_string_claim(decoded_token, "email");

  if (!email_opt) {
    throw http::bad_request("TDEI token contains no email");
  }

  const auto name_opt = try_string_claim(decoded_token, "name");

  if (!name_opt) {
    throw http::bad_request("TDEI token contains no name");
  }

  auto rw_transaction = update_factory.get_default_transaction();
  auto data_update = update_factory.make_data_update(*rw_transaction);

  const auto id = data_update->provision_tdei_user(subject, *email_opt, *name_opt);
  data_update->commit();

  logger::message(fmt::format("User {} created for TDEI user {}", id, subject));

  return id;
}

} // anonymous namespace


namespace workspaces {

std::optional<osm_user_id_t> authenticate_user(
  const request& req,
  data_selection& selection,
  data_update::factory& update_factory)
{
  const auto token_opt = try_token_from_request(req);

  if (!token_opt) {
    return std::nullopt;
  }

  const auto decoded_token_opt = try_decode_token(*token_opt);

  if (!decoded_token_opt) {
    return std::nullopt;
  }

  const auto decoded_token = *decoded_token_opt;

  //if (!verify_token(decoded_token)) {
  //  logger::message("TDEI token verification failed");
  //  return std::nullopt;
  //}

  // TODO: use the token subject
  const auto subject_opt = try_string_claim(decoded_token, "sub");

  if (!subject_opt) {
    logger::message("TDEI token contains no subject");
    return std::nullopt;
  }

  const auto user_id_opt = selection.get_user_id_for_tdei_token(*subject_opt);

  if (user_id_opt) {
    return user_id_opt;
  }

  return provision_user(update_factory, *subject_opt, decoded_token);
}

} // namespace workspaces
