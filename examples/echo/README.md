# IRC Echo Example

This example application relay message sent by users.

## Configuration
- Run `idf.py menuconfig` in this directory
- Navigate to `Example Configuration` and fill in the details.

- If your IRC network requires TLS connection:
  - Enable TLS support in `Component config -> ESPIRC Settings`
  - Enable `SSL/TLS Connection` in `Example Configuration -> IRC Settings`

- Build and flash the example
 
## Demo

### Client
```
<@danct12> hello world!
<@espircecho> danct12 (~danct12@hackint/user/danct12) sent: hello world!
```

### ESP32 Output:
```
(61474) IRC_Echo: danct121 (~danct12@hackint/user/danct12) in #funderscore-sucks sent: "hello world!"
```
