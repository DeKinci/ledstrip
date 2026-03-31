// Package proto implements minimal MicroProto message parsing for the gateway.
// Only parses what the gateway needs (HELLO); other messages are forwarded opaque.
package proto

// Opcodes
const (
	OpHello          = 0x0
	OpPropertyUpdate = 0x1
	OpSchemaUpsert   = 0x3
)

// HELLO flags
const (
	FlagIsResponse = 0x01
	FlagIdle       = 0x02
)

// EncodeHello builds a HELLO message (activate or deactivate)
func EncodeHello(idle bool) []byte {
	flags := byte(0)
	if idle {
		flags |= FlagIdle
	}

	buf := make([]byte, 0, 16)
	buf = append(buf, (byte(OpHello) | (flags << 4))) // op header
	buf = append(buf, 1)                               // protocol version
	buf = appendVarint(buf, 4096)                      // max packet size
	buf = appendVarint(buf, 0)                         // device_id (0 = gateway)
	buf = append(buf, 0, 0)                            // schema_version = 0
	return buf
}

// ParseHelloFlags extracts flags from a HELLO message header byte
func ParseHelloFlags(header byte) (isResponse bool, idle bool) {
	flags := (header >> 4) & 0x0F
	return flags&FlagIsResponse != 0, flags&FlagIdle != 0
}

// IsHello checks if a message is a HELLO opcode
func IsHello(data []byte) bool {
	return len(data) > 0 && (data[0]&0x0F) == OpHello
}

// GetOpcode returns the opcode from a message
func GetOpcode(data []byte) byte {
	if len(data) == 0 {
		return 0xFF
	}
	return data[0] & 0x0F
}

func appendVarint(buf []byte, value uint32) []byte {
	for value > 0x7F {
		buf = append(buf, byte(0x80|(value&0x7F)))
		value >>= 7
	}
	buf = append(buf, byte(value&0x7F))
	return buf
}
