/* mbed Microcontroller Library
 * Copyright (c) 2018-2018 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include "nRF5xPalSecurityManager.h"
#include "nrf_ble.h"
#include "nrf_ble_gap.h"
#include "nrf_soc.h"

namespace ble {
namespace pal {
namespace vendor {
namespace nordic {

namespace {
static ble_error_t convert_sd_error(uint32_t err) {
    switch (err) {
        case NRF_SUCCESS:
            return BLE_ERROR_NONE;
        case NRF_ERROR_INVALID_ADDR:
        case NRF_ERROR_INVALID_PARAM:
        case BLE_ERROR_INVALID_CONN_HANDLE:
            return BLE_ERROR_INVALID_PARAM;
        case NRF_ERROR_INVALID_STATE:
        case BLE_ERROR_INVALID_ROLE:
            return BLE_ERROR_INVALID_STATE;
        case NRF_ERROR_NOT_SUPPORTED:
            return BLE_ERROR_NOT_IMPLEMENTED;
        case NRF_ERROR_BUSY:
            return BLE_STACK_BUSY;
        case NRF_ERROR_TIMEOUT:
            return BLE_ERROR_INVALID_STATE;
        case NRF_ERROR_NO_MEM:
            return BLE_ERROR_NO_MEM;
        default:
            return BLE_ERROR_UNSPECIFIED;
    }
}
}

enum pairing_role_t {
    PAIRING_INITIATOR,
    PAIRING_RESPONDER
};

struct nRF5xSecurityManager::pairing_control_block_t {
    pairing_control_block_t* next;
    connection_handle_t connection;
    pairing_role_t role;

    // flags of the key present
    KeyDistribution initiator_dist;
    KeyDistribution responder_dist;

    // own keys
    ble_gap_enc_key_t own_enc_key;
    ble_gap_id_key_t own_id_key;
    ble_gap_sign_info_t own_sign_key;
    ble_gap_lesc_p256_pk_t own_pk;

    // peer keys
    ble_gap_enc_key_t peer_enc_key;
    ble_gap_id_key_t peer_id_key;
    ble_gap_sign_info_t peer_sign_key;
    ble_gap_lesc_p256_pk_t peer_pk;
};

nRF5xSecurityManager::nRF5xSecurityManager()
    : ::ble::pal::SecurityManager(),
    _io_capability(io_capability_t::NO_INPUT_NO_OUTPUT),
    _min_encryption_key_size(7),
    _max_encryption_key_size(16),
    _control_blocks(NULL)
{

}

nRF5xSecurityManager::~nRF5xSecurityManager()
{

}

////////////////////////////////////////////////////////////////////////////
// SM lifecycle management
//

ble_error_t nRF5xSecurityManager::initialize()
{
    // FIXME: generate and set public and private keys
    return BLE_ERROR_NONE;
}

ble_error_t nRF5xSecurityManager::terminate()
{
    return BLE_ERROR_NONE;
}

ble_error_t nRF5xSecurityManager::reset()
{
    // FIXME: reset public and private keys
    return BLE_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////
// Resolving list management
//

// FIXME: on nordic, the irk is passed in sd_ble_gap_scan_start where whitelist
// and resolving list are all mixed up.

uint8_t nRF5xSecurityManager::read_resolving_list_capacity()
{
    // FIXME: implement with privacy
    return 0;
}

ble_error_t nRF5xSecurityManager::add_device_to_resolving_list(
    advertising_peer_address_type_t peer_identity_address_type,
    const address_t &peer_identity_address,
    const irk_t &peer_irk
) {
    // FIXME: implement with privacy
    return BLE_ERROR_NOT_IMPLEMENTED;
}

ble_error_t nRF5xSecurityManager::remove_device_from_resolving_list(
    advertising_peer_address_type_t peer_identity_address_type,
    const address_t &peer_identity_address
) {
    // FIXME: implement with privacy
    return BLE_ERROR_NOT_IMPLEMENTED;
}

ble_error_t nRF5xSecurityManager::clear_resolving_list()
{
    // FIXME: implement with privacy
    return BLE_ERROR_NOT_IMPLEMENTED;
}

////////////////////////////////////////////////////////////////////////////
// Pairing
//


ble_error_t nRF5xSecurityManager::send_pairing_request(
    connection_handle_t connection,
    bool oob_data_flag,
    AuthenticationMask authentication_requirements,
    KeyDistribution initiator_dist,
    KeyDistribution responder_dist
)  {
    // allocate the control block required for the procedure completion
    pairing_control_block_t* pairing_cb = allocate_pairing_cb(connection);
    if (!pairing_cb) {
        return BLE_ERROR_NO_MEM;
    }
    pairing_cb->role = PAIRING_INITIATOR;

    // override signing parameter
    initiator_dist.set_signing(false);
    responder_dist.set_signing(false);

    ble_gap_sec_params_t security_params = make_security_params(
        oob_data_flag,
        authentication_requirements,
        initiator_dist,
        responder_dist
    );

    uint32_t err = sd_ble_gap_authenticate(
        connection,
        &security_params
    );

    if (err) {
        release_pairing_cb(pairing_cb);
    }

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::send_pairing_response(
    connection_handle_t connection,
    bool oob_data_flag,
    AuthenticationMask authentication_requirements,
    KeyDistribution initiator_dist,
    KeyDistribution responder_dist
) {
    pairing_control_block_t* pairing_cb = allocate_pairing_cb(connection);
    if (!pairing_cb) {
        return BLE_ERROR_NO_MEM;
    }
    pairing_cb->role = PAIRING_RESPONDER;

    // override signing parameter
    initiator_dist.set_signing(false);
    responder_dist.set_signing(false);

    ble_gap_sec_params_t security_params = make_security_params(
        oob_data_flag,
        authentication_requirements,
        initiator_dist,
        responder_dist
    );

    ble_gap_sec_keyset_t keyset = make_keyset(
        *pairing_cb,
        initiator_dist,
        responder_dist
    );

    uint32_t err = sd_ble_gap_sec_params_reply(
        connection,
        /* status */ BLE_GAP_SEC_STATUS_SUCCESS,
        /* params */ &security_params,
        /* keys */ &keyset
    );

    if (err) {
        release_pairing_cb(pairing_cb);
    }

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::cancel_pairing(
    connection_handle_t connection, pairing_failure_t reason
) {
    // this is the default path except when a key is expected to be enterred by
    // the user.
    uint32_t err = sd_ble_gap_sec_params_reply(
        connection,
        reason.value() | 0x80,
        /* sec params */ NULL,
        /* keyset */ NULL
    );

    if (!err) {
        return BLE_ERROR_NONE;
    }

    // Failed because we're in the wrong state; try to cancel pairing with
    // sd_ble_gap_auth_key_reply
    if (err == NRF_ERROR_INVALID_STATE) {
        err = sd_ble_gap_auth_key_reply(
            connection,
            /* key type */ BLE_GAP_AUTH_KEY_TYPE_NONE,
            /* key */ NULL
        );
    }

    return convert_sd_error(err);
}

////////////////////////////////////////////////////////////////////////////
// Feature support
//

ble_error_t nRF5xSecurityManager::get_secure_connections_support(
    bool &enabled
) {
    // NRF5x platforms support secure connections
    // FIXME: set to true once the ECC library is included
    enabled = false;
    return BLE_ERROR_NONE;
}

ble_error_t nRF5xSecurityManager::set_io_capability(io_capability_t io_capability)
{
    _io_capability = io_capability;
    return BLE_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////
// Security settings
//

ble_error_t nRF5xSecurityManager::set_authentication_timeout(
    connection_handle_t connection, uint16_t timeout_in_10ms
) {
    // FIXME: Use sd_ble_opt_set(BLE_GAP_OPT_AUTH_PAYLOAD_TIMEOUT, ...) when
    // available
    return BLE_ERROR_NOT_IMPLEMENTED;
}

ble_error_t nRF5xSecurityManager::get_authentication_timeout(
    connection_handle_t connection, uint16_t &timeout_in_10ms
) {
    // Return default value for now (30s)
    timeout_in_10ms = 30 * 100;
    return BLE_ERROR_NONE;
}

ble_error_t nRF5xSecurityManager::set_encryption_key_requirements(
    uint8_t min_encryption_key_size,
    uint8_t max_encryption_key_size
) {
    if ((min_encryption_key_size < 7) || (min_encryption_key_size > 16) ||
        (min_encryption_key_size > max_encryption_key_size)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    _min_encryption_key_size = min_encryption_key_size;
    _max_encryption_key_size = max_encryption_key_size;

    return BLE_ERROR_NONE;
}

ble_error_t nRF5xSecurityManager::slave_security_request(
    connection_handle_t connection,
    AuthenticationMask authentication
) {
    // In the peripheral role, only the bond, mitm, lesc and keypress fields of
    // this structure are used.
    ble_gap_sec_params_t security_params = {
        /* bond */ authentication.get_bondable(),
        /* mitm */ authentication.get_mitm(),
        /* lesc */ authentication.get_secure_connections(),
        /* keypress */ authentication.get_keypress_notification(),
        /* remainder of the data structure is ignored */ 0
    };

    uint32_t err = sd_ble_gap_authenticate(
        connection,
        &security_params
    );

    return convert_sd_error(err);
}

////////////////////////////////////////////////////////////////////////////
// Encryption
//

ble_error_t nRF5xSecurityManager::enable_encryption(
    connection_handle_t connection,
    const ltk_t &ltk,
    const rand_t &rand,
    const ediv_t &ediv,
    bool mitm
) {
    ble_gap_master_id_t master_id;
    memcpy(master_id.rand, rand.data(), rand.size());
    memcpy(&master_id.ediv, ediv.data(), ediv.size());

    ble_gap_enc_info_t enc_info;
    memcpy(enc_info.ltk, ltk.data(), ltk.size());
    enc_info.lesc = false;
    enc_info.auth = mitm;

    // FIXME: how to pass the lenght of the LTK ???
    enc_info.ltk_len = ltk.size();

    uint32_t err = sd_ble_gap_encrypt(
        connection,
        &master_id,
        &enc_info
    );

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::enable_encryption(
    connection_handle_t connection,
    const ltk_t &ltk,
    bool mitm
) {
    ble_gap_master_id_t master_id = {0};

    ble_gap_enc_info_t enc_info;
    memcpy(enc_info.ltk, ltk.data(), ltk.size());
    enc_info.lesc = true;
    enc_info.auth = mitm;
    enc_info.ltk_len = ltk.size();

    uint32_t err = sd_ble_gap_encrypt(
        connection,
        &master_id,
        &enc_info
    );

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::encrypt_data(
    const byte_array_t<16> &key,
    encryption_block_t &data
) {
    // FIXME: With signing ?
    return BLE_ERROR_NOT_IMPLEMENTED;
}

////////////////////////////////////////////////////////////////////////////
// Privacy
//

ble_error_t nRF5xSecurityManager::set_private_address_timeout(
    uint16_t timeout_in_seconds
) {
    // get the previous config
    ble_gap_irk_t irk;
    ble_opt_t privacy_config;
    privacy_config.gap_opt.privacy.p_irk = &irk;

    uint32_t err = sd_ble_opt_get(BLE_GAP_OPT_PRIVACY, &privacy_config);
    if (err) {
        return convert_sd_error(err);
    }

    // set the timeout and return the result
    privacy_config.gap_opt.privacy.interval_s = timeout_in_seconds;
    err = sd_ble_opt_set(BLE_GAP_OPT_PRIVACY, &privacy_config);
    return convert_sd_error(err);
}

////////////////////////////////////////////////////////////////////////////
// Keys
//

ble_error_t nRF5xSecurityManager::set_ltk(
    connection_handle_t connection,
    const ltk_t& ltk,
    bool mitm,
    bool secure_connections
) {
    ble_gap_enc_info_t enc_info;
    memcpy(enc_info.ltk, ltk.data(), ltk.size());
    enc_info.lesc = secure_connections;
    enc_info.auth = mitm;
    enc_info.ltk_len = ltk.size();

    // FIXME: provide peer irk and csrk ?
    uint32_t err = sd_ble_gap_sec_info_reply(
        connection,
        &enc_info,
        NULL,
        NULL // Not supported
    );

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::set_ltk_not_found(
    connection_handle_t connection
) {
    uint32_t err = sd_ble_gap_sec_info_reply(
        connection,
        NULL,
        NULL,
        NULL // Not supported
    );

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::set_irk(const irk_t& irk)
{
    // get the previous config
    ble_gap_irk_t sd_irk;
    ble_opt_t privacy_config;
    privacy_config.gap_opt.privacy.p_irk = &sd_irk;

    uint32_t err = sd_ble_opt_get(BLE_GAP_OPT_PRIVACY, &privacy_config);
    if (err) {
        return convert_sd_error(err);
    }

    // set the new irk
    memcpy(sd_irk.irk, irk.data(), irk.size());
    err = sd_ble_opt_set(BLE_GAP_OPT_PRIVACY, &privacy_config);

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::set_csrk(const csrk_t& csrk)
{
    _csrk = csrk;
    return BLE_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////
// Authentication
//

ble_error_t nRF5xSecurityManager::get_random_data(byte_array_t<8> &random_data)
{
    uint32_t err = sd_rand_application_vector_get(
        random_data.buffer(), random_data.size()
    );
    return convert_sd_error(err);
}

////////////////////////////////////////////////////////////////////////////
// MITM
//

ble_error_t nRF5xSecurityManager::set_display_passkey(passkey_num_t passkey)
{
    PasskeyAscii passkey_ascii(passkey);
    ble_opt_t sd_passkey;
    sd_passkey.gap_opt.passkey.p_passkey = passkey ? passkey_ascii.value() : NULL;
    uint32_t err = sd_ble_opt_set(BLE_GAP_OPT_PASSKEY, &sd_passkey);
    return convert_sd_error(err);
}


ble_error_t nRF5xSecurityManager::passkey_request_reply(
    connection_handle_t connection, const passkey_num_t passkey
) {
    PasskeyAscii pkasc(passkey);
    uint32_t err = sd_ble_gap_auth_key_reply(
        connection,
        BLE_GAP_AUTH_KEY_TYPE_PASSKEY,
        pkasc.value()
    );

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::legacy_pairing_oob_request_reply(
    connection_handle_t connection,
    const oob_tk_t& oob_data
) {
    uint32_t err = sd_ble_gap_auth_key_reply(
        connection,
        BLE_GAP_AUTH_KEY_TYPE_OOB,
        oob_data.data()
    );

    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::confirmation_entered(
    connection_handle_t connection, bool confirmation
) {
    uint32_t err = sd_ble_gap_auth_key_reply(
        connection,
        confirmation ? BLE_GAP_AUTH_KEY_TYPE_PASSKEY : BLE_GAP_AUTH_KEY_TYPE_NONE,
        NULL
    );
    return convert_sd_error(err);
}

ble_error_t nRF5xSecurityManager::send_keypress_notification(
    connection_handle_t connection, Keypress_t keypress
) {
    uint32_t err = sd_ble_gap_keypress_notify(
        connection,
        static_cast<uint8_t>(keypress)
    );
    return convert_sd_error(err);
}


ble_error_t nRF5xSecurityManager::generate_secure_connections_oob(
    connection_handle_t connection
) {
    // FIXME: made with external library; requires SDK v13
    return BLE_ERROR_NOT_IMPLEMENTED;
}


ble_error_t nRF5xSecurityManager::secure_connections_oob_received(
    const address_t &address,
    const oob_lesc_value_t &random,
    const oob_confirm_t &confirm
) {
    // FIXME: store locally
    return BLE_ERROR_NOT_IMPLEMENTED;
}

bool nRF5xSecurityManager::is_secure_connections_oob_present(
    const address_t &address
) {
    // FIXME: lookup local store
    return false;
}

nRF5xSecurityManager& nRF5xSecurityManager::get_security_manager()
{
    static nRF5xSecurityManager _security_manager;
    return _security_manager;
}

bool nRF5xSecurityManager::sm_handler(const ble_evt_t *evt)
{
    nRF5xSecurityManager& self = nRF5xSecurityManager::get_security_manager();
    SecurityManager::EventHandler* handler = self.get_event_handler();

    if ((evt == NULL) || (handler == NULL)) {
        return false;
    }

    const ble_gap_evt_t& gap_evt = evt->evt.gap_evt;
    uint16_t connection = gap_evt.conn_handle;
    pairing_control_block_t* pairing_cb = self.get_pairing_cb(connection);

    switch (evt->header.evt_id) {
        case BLE_GAP_EVT_SEC_PARAMS_REQUEST: {
            const ble_gap_sec_params_t& params =
                gap_evt.params.sec_params_request.peer_params;

            KeyDistribution initiator_dist(
                params.kdist_peer.enc,
                params.kdist_peer.id,
                params.kdist_peer.sign,
                params.kdist_peer.link
            );

            KeyDistribution responder_dist(
                params.kdist_own.enc,
                params.kdist_own.id,
                params.kdist_own.sign,
                params.kdist_own.link
            );

            if (pairing_cb && pairing_cb->role == PAIRING_INITIATOR) {
                // when this event is received by an initiator, it should not be
                // forwarded via the handler; this is not a behaviour expected
                // by the bluetooth standard ...
                ble_gap_sec_keyset_t keyset = make_keyset(
                    *pairing_cb,
                    initiator_dist,
                    responder_dist
                );
                uint32_t err = sd_ble_gap_sec_params_reply(
                    connection,
                    /* status */ BLE_GAP_SEC_STATUS_SUCCESS,
                    /* params */ NULL,
                    /* keys ... */ &keyset
                );

                // in case of error; release the pairing control block and signal
                // it to the event handler
                if (err) {
                    release_pairing_cb(pairing_cb);
                    handler->on_pairing_error(
                        connection,
                        pairing_failure_t::UNSPECIFIED_REASON
                    );
                }
            } else {
                AuthenticationMask authentication_requirements(
                    params.bond,
                    params.mitm,
                    params.lesc,
                    params.keypress
                );


                handler->on_pairing_request(
                    connection,
                    params.oob,
                    authentication_requirements,
                    initiator_dist,
                    responder_dist
                );
            }
            return true;
        }

        case BLE_GAP_EVT_SEC_INFO_REQUEST: {
            const ble_gap_evt_sec_info_request_t& req =
                gap_evt.params.sec_info_request;

            handler->on_ltk_request(
                connection,
                ediv_t((uint8_t*)(&req.master_id.ediv)),
                rand_t(req.master_id.rand)
            );

            return true;
        }

        case BLE_GAP_EVT_PASSKEY_DISPLAY: {
            const ble_gap_evt_passkey_display_t& req =
                gap_evt.params.passkey_display;

            if (req.match_request == 0) {
                handler->on_passkey_display(
                    connection,
                    PasskeyAscii::to_num(req.passkey)
                );
            } else {
                // FIXME handle this case for secure pairing
            }

            return true;
        }

        case BLE_GAP_EVT_KEY_PRESSED: {
            handler->on_keypress_notification(
                connection,
                (Keypress_t)gap_evt.params.key_pressed.kp_not
            );
            return true;
        }
        case BLE_GAP_EVT_AUTH_KEY_REQUEST: {
            uint8_t key_type = gap_evt.params.auth_key_request.key_type;

            switch (key_type) {
                case BLE_GAP_AUTH_KEY_TYPE_NONE: // Illegal
                    break;

                case BLE_GAP_AUTH_KEY_TYPE_PASSKEY:
                    handler->on_passkey_request(connection);
                    break;

                case BLE_GAP_AUTH_KEY_TYPE_OOB:
                    handler->on_legacy_pairing_oob_request(connection);
                    break;
            }

            return true;
        }

        case BLE_GAP_EVT_LESC_DHKEY_REQUEST:
            // FIXME: Add with LESC support
            return true;

        case BLE_GAP_EVT_AUTH_STATUS: {
            const ble_gap_evt_auth_status_t& status = gap_evt.params.auth_status;

            switch (status.auth_status) {
                // NOTE: pairing_cb must be valid if this event has been
                // received as it is being allocated earlier and release
                // in this block
                // The memory is released before the last call to the event handler
                // to free the heap a bit before subsequent allocation with user
                // code.
                case BLE_GAP_SEC_STATUS_SUCCESS: {
                    KeyDistribution own_dist;
                    KeyDistribution peer_dist;

                    if (pairing_cb->role == PAIRING_INITIATOR) {
                        own_dist = pairing_cb->initiator_dist;
                        peer_dist = pairing_cb->responder_dist;
                    } else {
                        own_dist = pairing_cb->responder_dist;
                        peer_dist = pairing_cb->initiator_dist;
                    }

                    if (own_dist.get_encryption()) {
                        handler->on_keys_distributed_local_ltk(
                            connection,
                            ltk_t(pairing_cb->own_enc_key.enc_info.ltk)
                        );

                        handler->on_keys_distributed_local_ediv_rand(
                            connection,
                            ediv_t(reinterpret_cast<uint8_t*>(
                                &pairing_cb->own_enc_key.master_id.ediv
                            )),
                            pairing_cb->own_enc_key.master_id.rand
                        );
                    }

                    if (peer_dist.get_encryption()) {
                        handler->on_keys_distributed_ltk(
                            connection,
                            ltk_t(pairing_cb->peer_enc_key.enc_info.ltk)
                        );

                        handler->on_keys_distributed_ediv_rand(
                            connection,
                            ediv_t(reinterpret_cast<uint8_t*>(
                                &pairing_cb->peer_enc_key.master_id.ediv
                            )),
                            pairing_cb->peer_enc_key.master_id.rand
                        );
                    }

                    if (peer_dist.get_identity()) {
                        handler->on_keys_distributed_irk(
                            connection,
                            irk_t(pairing_cb->peer_id_key.id_info.irk)
                        );

                        advertising_peer_address_type_t
                            address_type(advertising_peer_address_type_t::PUBLIC_ADDRESS);

                        if (pairing_cb->peer_id_key.id_addr_info.addr_type) {
                            address_type = advertising_peer_address_type_t::RANDOM_ADDRESS;
                        }

                        handler->on_keys_distributed_bdaddr(
                            connection,
                            address_type,
                            ble::address_t(pairing_cb->peer_id_key.id_addr_info.addr)
                        );
                    }

                    if (peer_dist.get_signing()) {
                        handler->on_keys_distributed_csrk(
                            connection,
                            pairing_cb->peer_sign_key.csrk
                        );
                    }

                    self.release_pairing_cb(pairing_cb);
                    handler->on_pairing_completed(connection);
                    break;
                }

                case BLE_GAP_SEC_STATUS_TIMEOUT:
                    self.release_pairing_cb(pairing_cb);
                    handler->on_pairing_timed_out(connection);
                    break;

                case BLE_GAP_SEC_STATUS_PASSKEY_ENTRY_FAILED:
                case BLE_GAP_SEC_STATUS_OOB_NOT_AVAILABLE:
                case BLE_GAP_SEC_STATUS_AUTH_REQ:
                case BLE_GAP_SEC_STATUS_CONFIRM_VALUE:
                case BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP:
                case BLE_GAP_SEC_STATUS_ENC_KEY_SIZE:
                case BLE_GAP_SEC_STATUS_SMP_CMD_UNSUPPORTED:
                case BLE_GAP_SEC_STATUS_UNSPECIFIED:
                case BLE_GAP_SEC_STATUS_REPEATED_ATTEMPTS:
                case BLE_GAP_SEC_STATUS_INVALID_PARAMS:
                case BLE_GAP_SEC_STATUS_DHKEY_FAILURE:
                case BLE_GAP_SEC_STATUS_NUM_COMP_FAILURE:
                case BLE_GAP_SEC_STATUS_BR_EDR_IN_PROG:
                case BLE_GAP_SEC_STATUS_X_TRANS_KEY_DISALLOWED:
                    self.release_pairing_cb(pairing_cb);
                    handler->on_pairing_error(
                        connection,
                        (pairing_failure_t::type) (status.auth_status & 0xF)
                    );
                    break;

                default:
                    self.release_pairing_cb(pairing_cb);
                    break;
            }

            return true;
        }

        case BLE_GAP_EVT_CONN_SEC_UPDATE: {
            const ble_gap_evt_conn_sec_update_t& req =
                gap_evt.params.conn_sec_update;

            if((req.conn_sec.sec_mode.sm == 1) && (req.conn_sec.sec_mode.lv >= 2)) {
                handler->on_link_encryption_result(
                    connection, link_encryption_t::ENCRYPTED
                );
            } else {
                handler->on_link_encryption_result(
                    connection, link_encryption_t::NOT_ENCRYPTED
                );
            }
            return true;
        }

        case BLE_GAP_EVT_TIMEOUT: {
            switch (gap_evt.params.timeout.src) {
                case BLE_GAP_TIMEOUT_SRC_SECURITY_REQUEST:
                    // Note: pairing_cb does not exist at this point; it is
                    // created when the module receive the pairing request.
                    handler->on_link_encryption_request_timed_out(connection);
                    return true;

                    // FIXME: enable with latest SDK
#if 0
                case BLE_GAP_TIMEOUT_SRC_AUTH_PAYLOAD:
                    handler->on_valid_mic_timeout(connection);
                    return true;
#endif
                default:
                    return false;
            }
            return false;
        }

        case BLE_GAP_EVT_SEC_REQUEST: {
            const ble_gap_evt_sec_request_t& req = gap_evt.params.sec_request;
            handler->on_slave_security_request(
                connection,
                AuthenticationMask(req.bond, req.mitm, req.lesc, req.keypress)
            );
            return true;
        }

        default:
            return false;
    }
}

ble_gap_sec_params_t nRF5xSecurityManager::make_security_params(
    bool oob_data_flag,
    AuthenticationMask authentication_requirements,
    KeyDistribution initiator_dist,
    KeyDistribution responder_dist
) {
    ble_gap_sec_params_t security_params = {
        /* bond */ authentication_requirements.get_bondable(),
        /* mitm */ authentication_requirements.get_mitm(),
        /* lesc */ authentication_requirements.get_secure_connections(),
        /* keypress */ authentication_requirements.get_keypress_notification(),
        /* io_caps */ _io_capability.value(),
        /* oob */ oob_data_flag,
        /* min_key_size */ _min_encryption_key_size,
        /* max_key_size */ _max_encryption_key_size,
        /* kdist_periph */ {
            /* enc */ responder_dist.get_encryption(),
            /* id */ responder_dist.get_identity(),
            /* sign */ responder_dist.get_signing(),
            /* link */ responder_dist.get_link()
        },
        /* kdist_central */ {
            /* enc */ initiator_dist.get_encryption(),
            /* id */ initiator_dist.get_identity(),
            /* sign */ initiator_dist.get_signing(),
            /* link */ initiator_dist.get_link()
        }
    };
    return security_params;
}

ble_gap_sec_keyset_t nRF5xSecurityManager::make_keyset(
    pairing_control_block_t& pairing_cb,
    KeyDistribution initiator_dist,
    KeyDistribution responder_dist
) {
    pairing_cb.initiator_dist = initiator_dist;
    pairing_cb.responder_dist = responder_dist;

    KeyDistribution* own_dist = NULL;
    KeyDistribution* peer_dist = NULL;

    if (pairing_cb.role == PAIRING_INITIATOR) {
        own_dist = &initiator_dist;
        peer_dist = &responder_dist;
    } else {
        own_dist = &responder_dist;
        peer_dist = &initiator_dist;
    }

    ble_gap_sec_keyset_t keyset = {
        /* keys_own */ {
            own_dist->get_encryption() ? &pairing_cb.own_enc_key : NULL,
            own_dist->get_identity() ? &pairing_cb.own_id_key : NULL,
            own_dist->get_signing() ? &pairing_cb.own_sign_key : NULL,
            own_dist->get_link() ? &pairing_cb.own_pk : NULL
        },
        /* keys_peer */ {
            peer_dist->get_encryption() ? &pairing_cb.peer_enc_key : NULL,
            peer_dist->get_identity() ? &pairing_cb.peer_id_key : NULL,
            peer_dist->get_signing() ? &pairing_cb.peer_sign_key : NULL,
            peer_dist->get_link() ? &pairing_cb.peer_pk : NULL
        }
    };

    // copy csrk if necessary
    if (keyset.keys_own.p_sign_key) {
        memcpy(keyset.keys_own.p_sign_key->csrk, _csrk.data(), _csrk.size());
    }

    return keyset;
}

nRF5xSecurityManager::pairing_control_block_t*
nRF5xSecurityManager::allocate_pairing_cb(connection_handle_t connection)
{
    pairing_control_block_t* pairing_cb =
        new (std::nothrow) pairing_control_block_t();
    if (pairing_cb)  {
        pairing_cb->next = _control_blocks;
        _control_blocks = pairing_cb;
    }
    return pairing_cb;
}

void nRF5xSecurityManager::release_pairing_cb(pairing_control_block_t* pairing_cb)
{
    if (pairing_cb == _control_blocks) {
        _control_blocks = _control_blocks->next;
        delete pairing_cb;
    } else {
        pairing_control_block_t* it = _control_blocks;
        while (it->next) {
            if (it->next == pairing_cb) {
                it->next = pairing_cb->next;
                delete pairing_cb;
                return;
            }
            it = it->next;
        }
    }
}

nRF5xSecurityManager::pairing_control_block_t*
nRF5xSecurityManager::get_pairing_cb(connection_handle_t connection)
{
    pairing_control_block_t* pcb = _control_blocks;
    while (pcb) {
        if (pcb->connection == connection) {
            return pcb;
        }
        pcb = pcb->next;
    }

    return NULL;
}

} // nordic
} // vendor
} // pal
} // ble
