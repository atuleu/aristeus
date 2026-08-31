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

func (opcode RACPOpcode) String() string {
	switch opcode {
	case RACP_opcode_reserved:
		return "RESERVED"
	case RACP_opcode_report_records:
		return "REPORT_RECORDS"
	case RACP_opcode_delete_records:
		return "DELETE_RECORDS"
	case RACP_opcode_abord_operation:
		return "ABORD_OPERATION"
	case RACP_opcode_report_number:
		return "REPORT_NUMBER_OF_RECORDS"
	case RACP_opcode_response_number:
		return "RESPONSE_NUMBER_OF_RECORD"
	case RACP_opcode_response:
		return "RESPONSE"
	default:
		return "<UNKNOWN>"
	}
}

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

func (c RACPResponseCode) String() string {
	switch c {
	case RACP_response_reserved:
		return "RESERVED"
	case RACP_response_success:
		return "SUCCESS"
	case RACP_response_opcode_not_supported:
		return "OPCODE_NOT_SUPPORTED"
	case RACP_response_invalid_operator:
		return "INVALID_OPERATOR"
	case RACP_response_operator_not_supported:
		return "OPERATOR_NOT_SUPPORTED"
	case RACP_response_invalid_operand:
		return "INVALID_OPERAND"
	case RACP_response_no_records_found:
		return "NO_RECORD_FOUND"
	case RACP_response_abort_unsuccessful:
		return "ABORT_UNSUCESSFUL"
	case RACP_response_procedure_not_completed:
		return "PROCEDURE_NOT_COMPLETED"
	case RACP_response_operand_not_supported:
		return "OPERAND_NOT_SUPPORTED"
	case RACP_response_server_busy:
		return "SERVER_BUSY"
	default:
		return "<UNKNOWN>"
	}
}
