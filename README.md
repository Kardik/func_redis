# func_redis

**Asterisk 20+ port.** This is a fork of [tic-ull/func_redis](https://github.com/tic-ull/func_redis),
an Asterisk module that exposes Redis to the dialplan. It uses
[hiredis](https://github.com/redis/hiredis) as the Redis client library.

The original module was written for Asterisk 11/13 and no longer builds on
modern Asterisk and hiredis. This port targets **Asterisk 20 and 22 (LTS) and
newer**, and **hiredis 1.x** (0.14 should also work). The dialplan API is
unchanged.

## Motivation of the project

This project is motivated by the need to share information between different
asterisk in both active-active and active-passive scheme.

In the case of passive-active scheme if you use AstDB for storing data,
when system switch over, the data in AstDB isn't in the passive node,
so some functionality is lost, using Redis instead of AstDB, the active
node and the passive one can access the data, even the possibility of
making an passive active redis scheme in this case the active asterisk
attack the active redis and the passive asterisk attacks the passive
redis with this scheme you can have on the same machine redis and Asterisk.

In the case of both active-active scheme asterisk can share information easily by redis.

func_redis is not a drop in replacement of AstDB, internally asterisk uses AstDB,
for example to keep registry of the phones.

You can use an agi script or a system call to a script to use redis from the dialplan
but the performance is low compared to a asterisk module and the integration is worst.

## What changed relative to upstream

- Removed `ASTERISK_FILE_VERSION()` (removed from Asterisk 20).
- Connection lifecycle rewritten: the module no longer pokes inside the
  `redisContext`/`redisReader` structs (private/opaque in hiredis 1.x).
  Connections are per-thread, reconnect automatically after connection
  errors, and are released with `redisFree()`.
- `REDIS_COMMAND()` now tokenizes the command line properly (the upstream
  version passed user input as a hiredis format string, so multi-word
  commands did not work reliably and `%` characters could crash it).
  Double quotes can be used for arguments containing spaces.
- Fixed memory corruption when formatting array replies containing
  integer or nil elements (upstream used `reply->len` on element types
  where that field is undefined).
- Fixed non NUL-terminated copies (`strncpy`) and an off-by-one in the
  integer-to-string buffer size.
- `redis show` CLI command now reports keys whose value is not a string
  (e.g. lists, hashes) instead of misreporting errors.
- Config parsing hardened (port/timeout are range-checked via
  `ast_parse_arg`).
- Asterisk XML documentation is generated from the `DOCUMENTATION` block and
  installed to `<asterisk-datadir>/documentation/thirdparty/`, so
  `core show function REDIS` prints real help and the
  "Couldn't find function REDIS in XML documentation" warnings disappear
  (run `xmldoc reload` or restart Asterisk after installing).
- Build system modernized: CMake >= 3.10, hiredis located via
  `find_library`, multiarch Asterisk module directories supported.

## Dependencies

- gcc / build-essential
  - [Debian/Ubuntu] `apt-get install build-essential`
- cmake >= 3.10
  - [Debian/Ubuntu] `apt-get install cmake`
- Asterisk development headers (Asterisk 20+)
  - [Debian/Ubuntu] `apt-get install asterisk-dev`
- hiredis development files
  - [Debian/Ubuntu] `apt-get install libhiredis-dev`
- redis-server (runtime)
  - [Debian/Ubuntu] `apt-get install redis-server`

## Installation

1. Install the dependencies (see above).
2. `mkdir build`
3. `cd build`
4. `cmake -DCMAKE_BUILD_TYPE=Release ..`
5. `make -j$(nproc)`
6. `sudo make install`
7. `sudo make samples` — copies `samples/func_redis.conf.sample` to
   `/etc/asterisk/func_redis.conf` (adjust to your Asterisk config dir).
8. The build also generates `func_redis-en_US.xml` — the Asterisk XML
   documentation for the module's dialplan functions — and `make install`
   places it in `${ASTERISK_DATA_DIR}/documentation/thirdparty/`.
   Asterisk's data directory is auto-detected (Debian/Ubuntu:
   `/usr/share/asterisk`; vanilla builds: `/var/lib/asterisk`; override with
   `-DASTERISK_DATA_DIR=/path`). On a running Asterisk refresh the
   documentation with `asterisk -rx "xmldoc reload"` (or restart). Without
   this file every module load logs
   "Couldn't find function REDIS in XML documentation" and
   `core show function REDIS` prints no help.
9. Ensure the module is loaded: either keep `autoload=yes` in
   `modules.conf`, or add `load => func_redis.so`, then restart Asterisk
   or run `module load func_redis.so` in the Asterisk CLI.

## Build with Docker

A multi-stage `Dockerfile` is included. It builds the module against the
Asterisk 20 headers from Debian bookworm and, by default, runs a smoke
test that loads the module into a real Asterisk, exercises the CLI
commands and runs a small dialplan exercising `REDIS()` and
`REDIS_DELETE()`:

```
docker build -t func_redis .
```

Compile only (no runtime test):

```
docker build --target build -t func_redis:build .
```

## Uninstall

- `make uninstall`

## Using func_redis

In order to use the func_redis you have to configure the settings for the module
in the file func_redis.conf. There is an example in samples/func_redis.conf.sample, if you
run make samples it will copy this file to /etc/asterisk

Here an example of the file :

```
[general]
; host of the redis server
; if not defined, use default of 127.0.0.1
hostname=127.0.0.1

; port of the redis server
; if not defined, use default of 6379
port=6379

; database index in redis
; if not defined, use default of 0
database=0

; password for redis server
; if not defined, authentication will not be used
;password=s3cr3tp@ssw0rd

; connection time out when connecting to the server, in seconds
; if not defined, use a default of 5 seconds
timeout=3

; send a BGSAVE when a connection is closed and on module unload
; if not defined, use a default of false
bgsave=false
```

### Using func_redis from the Dialplan

Func redis have two apis one similar to AstDB and another generic, if you don't want to
learn how redis work use the AstDB API but if you know how redis work you can use the generic API
that is more powerful. All the redis dialplan functions save their results in REDIS_RESULT, it is a string always.
If the response is a redis array it puts the elements in a coma separated way like `value1,value2,value3`.
If the result is a redis nil the REDIS_RESULT is equal to `"nil"`. If there are errors the channel variable
REDIS_ERROR is filled with the error.

#### AstDB API

##### Set a key value

`same => n,Set(REDIS(key)=${VALUE})`

##### Get the value from a key

`same => n,Set(VALUE=${REDIS(key)})`

##### Delete a key

`same => n,NoOp(Deleting test key ${REDIS_DELETE(key)})`

or, from a write operation (works even with `live_dangerously=no`):

`same => n,Set(REDIS_DELETE(key)=1)`

##### Check if a key exists

`same => n,GotoIf($[${REDIS_EXISTS(key)} = 1]?exists:doesnt_exist)`

#### Generic API

##### Any redis command

Words are split on whitespace; use double quotes for arguments containing spaces.

```
same => n,NoOp(${REDIS_COMMAND(PUBLISH channel "some message")})
same => n,NoOp(${REDIS_COMMAND(MGET key1 key2 key3)})
same => n,NoOp(${REDIS_COMMAND(LPOP alist)})
```

### Using func_redis from the CLI

You can use these commands related to func_redis in the Asterisk CLI

1. `redis show [pattern]`
Shows all the key values.
[pattern] pattern to match keys
Examples :
- `h?llo` matches hello, hallo and hxllo
- `h*llo` matches hllo and heeeello
- `h[ae]llo` matches hello and hallo, but not hillo

2. `redis set <key> <value>`
Sets the key's value.

3. `redis del <key>`
Deletes the key-value pair in redis.

## Develop environment

#### Start Developing

This repo have a Vagrantfile with the needed configuration to have a functional yet basic development environment.
To use it you should run

```
vagrant up
vagrant ssh
```

You can find the project in the vm in the folder /func_redis.
You can make the changes that you wish and then

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
make install
```

#### Testing the module

I don't use a proper test suite but in exchange I test the module making use of the dialplan,
call files and local channels, to run the test

`make dialplan_test`

The `Dockerfile` in this repository runs an equivalent smoke test automatically
(see "Build with Docker").

## Code security and tools

To check if the code uses insecure functions and make some security checks run

`make security_checks`

for security checks it uses the nexts static analyzers :

- cppcheck
- graudit
- flawfinder

## Contribute and collaborate

See CONTRIBUTING.md
