#pragma once

#include "sl_enum.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// definition from
// https://bitbucket.org/bluetooth-SIG/public/src/main/gss/org.bluetooth.characteristic.record_access_control_point.yaml

SL_ENUM(racp_opcode_t){
    racp_opcode_reserved            = 0x00,
    racp_opcode_report_records      = 0x01,
    racp_opcode_delete_records      = 0x02,
    racp_opcode_abort_operation     = 0x03, // operator must be null, no operand
    racp_opcode_report_number       = 0x04,
    racp_opcode_rsp_number_response = 0x05,
    racp_opcode_rsp_response_code   = 0x06,

};

SL_ENUM(racp_operator_t){
    racp_operator_null     = 0x00,
    racp_operator_all      = 0x01,
    racp_operator_le       = 0x02,
    racp_operator_ge       = 0x03,
    racp_operator_in_range = 0x04,
    racp_operator_first    = 0x05,
    racp_operator_last     = 0x06,
};

SL_ENUM(racp_rsp_t){
    /// Reserved for future use.
    racp_rsp_reserved                = 0x00,
    /// Normal response for successful operation.
    racp_rsp_sucess                  = 0x01,
    /// Error response if the unsupported Op Code is received.
    racp_rsp_opcode_not_supported    = 0x02,
    /// Error response if Operator received does not meet the requirement of the
    /// service (e.g. Null was expected or not).
    racp_rsp_invalid_operator        = 0x03,
    /// Error response if unsupported Operator is received.
    racp_rsp_operator_not_supported  = 0x04,
    /// Error response if Operand received does not meet the requirement of
    /// the service.
    racp_rsp_invalid_operand         = 0x05,
    /// Error response if no records found for the request that meet the
    /// criteria. With racp_opcode_report_number
    /// racp_opcode_rtsp_number_response with value 0 is used when no record are
    /// found.
    racp_rsp_code_no_record_found    = 0x06,
    /// Abort is sucessful.
    racp_rsp_code_abort_sucessful    = 0x07,
    /// Error response if procedure connot be completed for any reason.
    racp_rsp_procedure_not_completed = 0x08,
    /// Error response if unsupported operand is received.
    racp_rsp_operand_not_supported   = 0x09,
    /// Error response is the server is busy
    racp_rsp_server_busy             = 0x0a,

};

SL_ENUM(gatt_ecode_t){
    gatt_ecode_succeed                  = 0x00,
    gatt_ecode_procedure_in_progress    = 0x80,
    gatt_ecode_not_indicated            = 0x81,
    gatt_ecode_invalid_attribute_length = 0x0d,
    gatt_ecode_write_request_rejected   = 0xfc,
};

#ifdef __cplusplus
}
#endif // __cplusplus
