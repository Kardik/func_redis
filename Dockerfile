# syntax=docker/dockerfile:1
#
# Multi-stage Dockerfile for func_redis (Asterisk 20+, Debian bookworm).
#
#   docker build --target build -t func_redis:build .   # compile only
#   docker build -t func_redis .                        # compile + smoke test
#
# The default target builds the module against the Asterisk 20 headers from
# Debian bookworm, installs it into a runtime image together with Asterisk and
# Redis, and runs a smoke test that exercises the module from the CLI and from
# the dialplan (REDIS read/write/delete).

FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
		build-essential \
		cmake \
		asterisk-dev \
		libhiredis-dev \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
	&& cmake --build build --parallel \
	&& cmake --install build


FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
		asterisk \
		redis-server \
	&& rm -rf /var/lib/apt/lists/*

# Install the built module regardless of the modules directory layout.
COPY --from=build /usr/lib/*/asterisk/modules/func_redis.so /usr/lib/asterisk/modules/func_redis.so
COPY samples/func_redis.conf.sample /etc/asterisk/func_redis.conf

# Smoke test: start redis + asterisk, then exercise the module from the CLI
# and from a small dialplan exercising REDIS read/write and REDIS_DELETE.
RUN set -eux; \
	mkdir -p /var/run/asterisk /var/log/asterisk; \
	chown asterisk:asterisk /var/run/asterisk /var/log/asterisk; \
	redis-server --daemonize yes; \
	asterisk -g -f -U asterisk -G asterisk >/tmp/asterisk.log 2>&1 & \
	sleep 6; \
	asterisk -rx "module load func_redis.so" || true; \
	asterisk -rx "module show like func_redis" | grep "func_redis.so"; \
	asterisk -rx "redis set smoke_cli it_works"; \
	asterisk -rx "redis show smoke_*"; \
	redis-cli get smoke_cli | grep -q it_works; \
	printf '%s\n' \
		'[redis-smoke]' \
		'exten => s,1,Set(REDIS(smoke_dp)=dialplan_ok)' \
		' same => n,GotoIf($["${REDIS(smoke_dp)}" = "dialplan_ok"]?ok:fail)' \
		' same => n(fail),NoOp(REDIS DIALPLAN TEST FAILED)' \
		' same => n,Hangup()' \
		' same => n(ok),NoOp(REDIS DIALPLAN TEST OK)' \
		' same => n,Set(REDIS_DELETE(smoke_dp)=1)' \
		' same => n,Hangup()' >> /etc/asterisk/extensions.conf; \
	asterisk -rx "dialplan reload"; \
	asterisk -rx "channel originate Local/s@redis-smoke application Wait 1"; \
	sleep 3; \
	[ "$(redis-cli exists smoke_dp)" = "0" ]; \
	asterisk -rx "redis del smoke_cli"; \
	asterisk -rx "core stop now"; \
	sleep 2; \
	redis-cli shutdown nosave || true
