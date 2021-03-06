# Kapseis RPC

Kapseis RPC is fully secured, with TLS.
The connection to the server is persistent to reduce latency.
The protocol is asynchronous.

`example.wes`
```
import std.String;

message Identity_User {
	String name @1;
}

message Identity_CreateUserRequest {
	Identity_User user @1;
}

message Identity_Error {
	String desc @1;
}

response Identity_GenericResponses {
	Identity_Error internal_server_error;
}

response Identity_CreateUserResponse {
	Identity_GenericResponses @2;
	Identity_User ok;
}

Identity_CreateUserResponse identity_create_user(Identity_CreateUserRequest) @0x94BE_0166_BBED_2DE0;
```

## Statuses

0xFF is reserved.

## Protocol

Functions with TLS-over-TCP.

Procedure IDs 0-127 are reserved for the protocol.

```
message Void {}
response VoidResponse {
	Void ok;
}
```

Built-in procedure IDs:
- 0: `VoidResponse ping(Void) @0x00`

Request: `FF <procedure ID, u64> <response ID, vu64, 00 if not applicable> <payload length, vu64> <payload, bytes>`  
Response: `<status, u8> <response ID, vu64, never 00> <payload length, vu64> <response family ID> <payload, bytes>`

TODO(rutgerbrf): explain response family ID structure

```
Client -> server: identity_create_user({ user = { name = "Hello" } })

  | We're calling identity_create_user
  |
  |                        | Identity_CreateUserRequest.user
  |                        |
  |                        |   | Identity_User.name
  |                        |   |
  |                        |   |   | Identity_User.name's contents (a string) is 5 characters long
  |                        |   |   |
  |                        |   |   |   | 'Hello' (contens of Identity_User.name)
  |                        |   |   |   |
  |                        |   |   |   |
  |                        |   |   |   |
  |                        |   |   |   |               | End of Identity_CreateUserRequest.user (Identity_User)
  |_______________________ |__ |__ |__ |______________ |__
  |                       \|  \|  \|  \|              \|  \
   94 be 01 66 bb ed 2d e0  01  01  05  48 65 6c 6c 6f  00
```

