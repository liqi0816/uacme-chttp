# uacme-chttp

A bare minimal HTTP server to solve ACME 2.0 http-01 challenges.

## Description

This naïve ~150 lines of code removes the need to install netcat, socat, python, busybox, nginx, httpd etc if you want a certificate for non website servers.

## Usage

It is recommended not to run this program with `root` or special privileges. Instead, use `iptables`/`nft` to forward `80` requests to `44380`.

To integrate with `uacme`:

```
uacme --verbose --confdir /tmp/uacme.d --hook /path_to/uacme-chttp --staging issue examplle.com
```

As a standalone server:

```
UACME_CHTTP_ACCOUNT=account_fingerprint UACME_CHTTP_TIMEOUT=100 /path_to/uacme-chttp
```

The log message is prefixed with severity, and can be parsed by `systemd-journald`.

## Parameters

This program can accept exactly 0 or 6 command line arguments. The 6 arguments are the ones defined by `uacme`. They are expected to be:

```
./uacme-chttp begin http-01 (unused) CLI_TOKEN CLI_AUTH
```


If the incoming HTTP request does not match `GET /.well-known/acme-challenge/${CLI_TOKEN}`, a warning message will be printed.

This program can accept the following environment variables:

`UACME_CHTTP_ADDR`: defaults to `::`. The listen address. Use `iptables`/`nft` to forward requests to this address.

`UACME_CHTTP_PORT`: defaults to `44380` so that no special permission is required. The listen port. Use `iptables`/`nft` to forward requests to this port.

`UACME_CHTTP_TIMEOUT`: defaults to `10`. The process will exit after `UACME_CHTTP_TIMEOUT` seconds if there is no new connections.

`UACME_CHTTP_BINDTODEVICE`: defaults to unset. If defined, will listen on the network interface with name `UACME_CHTTP_BINDTODEVICE`, for example `eth0`.

`UACME_CHTTP_FORCE_DETACH`: defaults to unset. If defined, will force the process to fork and return `0`. The child process will accept connections in background.

`UACME_CHTTP_ACCOUNT`: defaults to unset. See below for explanation:

- If `UACME_CHTTP_ACCOUNT` is defined, the response content will be `${ACTUAL_TOKEN_FROM_REQUEST}.${UACME_CHTTP_ACCOUNT}`, where `ACTUAL_TOKEN_FROM_REQUEST` is the actual token extracted from the first line of the HTTP request. It is used to facilitate [stateless mode](https://github.com/acmesh-official/acme.sh/wiki/Stateless-Mode).
- If `UACME_CHTTP_ACCOUNT` is unset, the response content will be `${CLI_AUTH}`.
- If the response content cannot be determined, a warning message will be printed.

## Known quirks

Since this is a bare minimal implementation, some corner cases are not covered. Most competent clients should be able to handle them, but a bare minimal client may not.

1. This program will happily take environment variables that are empty strings. Please `unset` them to use the default values.
2. This program will start responding as soon as the first line of HTTP request is complete. It will not consume the rest lines. If the client keeps sending stuff after we have finished the response, a `RST` will be sent after `FIN`. 
3. This program will throw error if a client connects and then does nothing after 1 second.

## Build

```
gcc uacme-chttp.c -Wall -O3 -o uacme-chttp
```

## License

MIT
