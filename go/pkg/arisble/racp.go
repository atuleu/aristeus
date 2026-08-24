package arisble

type RACPOpcode uint8

const (
	RACP_opcode_reserved        RACPOpcode = 0x00
	RACP_opcode_report_records             = 0x01
	RACP_opcode_delete_records             = 0x02
	RACP_opcode_abord_operation            = 0x03
	RACP_opcode_report_number              = 0x04
	RACP_opcode_response_number            = 0x05
	RACP_opcode_response                   = 0x06
)

type RACPOperator uint8

const (
	RACP_operator_null     = 0x00
	RACP_operator_all      = 0x01
	RACP_operator_le       = 0x02
	RACP_operator_ge       = 0x03
	RACP_operator_in_range = 0x04
	RACP_operator_first    = 0x05
	RACP_operator_last     = 0x06
)

type RACPResponseCode uint8

const (
	RACP_response_reserved                = 0x00
	RACP_response_success                 = 0x01
	RACP_response_opcode_not_supported    = 0x02
	RACP_response_invalid_operator        = 0x03
	RACP_response_operator_not_supported  = 0x04
	RACP_response_invalid_operand         = 0x05
	RACP_response_no_records_found        = 0x06
	RACP_response_abort_unsuccessful      = 0x07
	RACP_response_procedure_not_completed = 0x08
	RACP_response_operand_not_supported   = 0x09
	RACP_response_server_busy             = 0x0A
)
