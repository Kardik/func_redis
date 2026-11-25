# syntax=docker/dockerfile:1
#
# Multi-stage Dockerfile for func_redis (Asterisk 20+, Ubuntu 24.04).
#
#   docker build --target build -t func_redis:build .   # compile only
#   docker build -t func_redis .                        # compile + smoke test
#
# The default target builds the module against the Asterisk 20 headers from
# Ubuntu 24.04 (noble), installs it into a runtime image together with Asterisk
# and Redis, and runs a smoke test that exercises the module from the CLI and
# from the dialplan (REDIS read/write/delete).
#
# Note: Debian bookworm cannot be used as the base because the asterisk
# package was removed from it; Ubuntu noble ships Asterisk 20.6.

FROM ubuntu:noble AS build

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


FROM ubuntu:noble AS runtime

# libhiredis is needed at runtime: func_redis.so links against libhiredis.so.
RUN apt-get update && apt-get install -y --no-install-recommends \
		asterisk \
		redis-server \
		libhiredis-dev \
	&& rm -rf /var/lib/apt/lists/*

# Stage the built artifacts.
COPY --from=build /src/build/func_redis.so /opt/func_redis/func_redis.so
COPY --from=build /src/build/func_redis-en_US.xml /opt/func_redis/func_redis-en_US.xml

# Install the module into the actual astmoddir and the XML documentation into
# the Asterisk data dir, as configured in asterisk.conf (Debian/Ubuntu use a
# multiarch module path, e.g. /usr/lib/x86_64-linux-gnu/asterisk/modules).
RUN set -e; \
	moddir=$(awk -F'=>' '/^[ \t]*astmoddir/{gsub(/[ \t]/,"",$2); print $2; exit}' /etc/asterisk/asterisk.conf); \
	moddir=${moddir:-/usr/lib/asterisk/modules}; \
	datadir=$(awk -F'=>' '/^[ \t]*astdatadir/{gsub(/[ \t]/,"",$2); print $2; exit}' /etc/asterisk/asterisk.conf); \
	datadir=${datadir:-/var/lib/asterisk}; \
	install -D -m 644 /opt/func_redis/func_redis.so "$moddir/func_redis.so"; \
	install -D -m 644 /opt/func_redis/func_redis-en_US.xml "$datadir/documentation/thirdparty/func_redis-en_US.xml"; \
	rm -rf /opt/func_redis

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
	asterisk -rx "module show like func_redis" | grep "func_redis.so" \
		|| { grep -i "loader\|func_redis" /tmp/asterisk.log; exit 1; }; \
	asterisk -rx "redis set smoke_cli it_works"; \
	asterisk -rx "redis show smoke_*"; \
	redis-cli get smoke_cli | grep -q it_works; \
	asterisk -rx "xmldoc reload"; \
	asterisk -rx "core show function REDIS" | grep -q "Read from or write to a Redis database"; \
	asterisk -rx "core show function REDIS_COMMAND" | grep -q "arbitrary command"; \
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
