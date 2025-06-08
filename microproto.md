ok so let's design a custom property-based protocol for our device. Let's support both properties and rpc. the protocol should be binary and very space-efficient.
Let's separate schema and actual data transition.
Supported types:
- bool (just a uint8)
- uint8
- int16
- int32
- flat32
- arrays of all types (array length is encoded in a unicode-like fasion, with last byte of length starting with 0.)
- utf8 string (just a binary array)
- utf8-value pair
let's also allow each property to be nested in a namespace

operations:
- short property update = 0 // when property ids fit in u8
- long property update = 1 // when property requires u16 for ids
- schema upsert = 2
- schema delete = 3


When websocket connects it will receive the current schema as a schema_upsert operation with a bunch of property definitions. From then it will receive mostly property updates, or schema_upsert/delete if those are needed.
Each message begins with a single u8 operation type.
```
u8 operation_type {
    opcode: bit4,
    _reserved: bit3,
    batch_flag: bit1
}
```
if batch flag is 1, after it a single u8 value will follow, specifying amount of batched together operations + 1 (up to 256).

schema should be basically CRUD operation on properties:
```
{
    schema_operation: operation_type(opcode = 2)
    // optional batching
    {
        meta: u8 {
            type: bit4 oneOf(NAMESPACE, PROPERTY, FUNCTION),
            _reserved: bit3,
            large_id: bit1 // whether to use u8 or u16 for ids
        },
        namespace_id: u16,
        name: utf8,
        description: utf8,
        data_type: u8,
        default_value: bits,
        validation: oneOf(
            // base on type

            /// here some work required
        )
    }
}
```

properties should be sent like this:
```
{
    property_update: u8 = operation_type(opcode = oneOf(0, 1)) // 0 for 1 byte id properties, 1 for 2 byte id properties
    // optional batching

    {
        id: u8/16,
        value: oneOf(
            bytes for simple types,
            {
                length: u8/u24,
                items: [...]
            } for arrays,
            bytes (tightly packed) for objects (static arrays are also objects)
        )
    },
    ...?

}
```