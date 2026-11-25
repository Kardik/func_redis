/*
 * func_redis.c
 *
 * Original Author : Sergio Medina Toledo <lumasepa at gmail>
 * https://github.com/tic-ull/func_redis
 *
 * Contributor : Alan Graham <ag at zerohalo>
 * https://github.com/zerohalo/func_redis
 *
 * Asterisk 20+ port : compatibility fixes and cleanups
 * https://github.com/Kardik/func_redis (asterisk-20 branch)
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Functions for interaction with Redis database
 *
 * \author Sergio Medina Toledo <lumasepa at gmail>
 * \author Alan Graham <ag at zerohalo>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
	<depend>hiredis</depend>
 ***/


#define AST_MODULE_SELF_SYM __internal_func_redis_self

#include <asterisk.h>

#include <asterisk/module.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/utils.h>
#include <asterisk/app.h>
#include <asterisk/cli.h>
#include <asterisk/config.h>
#include <asterisk/threadstorage.h>
#include <asterisk/strings.h>

#ifndef AST_MODULE
#define AST_MODULE "func_redis"
#endif

#include <hiredis/hiredis.h>

#include <ctype.h>
#include <stdarg.h>


/*** DOCUMENTATION
	<function name="REDIS" language="en_US">
		<synopsis>
			Read from or write to a Redis database.
		</synopsis>
		<syntax>
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>This function will read from or write a value to the Redis database.  On a
			read, this function returns the corresponding value from the database, or the
			string <literal>nil</literal> if it does not exist.  Reading a database value
			will also set the variable REDIS_RESULT.  If you wish to find out if an entry
			exists, use the REDIS_EXISTS function.</para>
		</description>
		<see-also>
			<ref type="function">REDIS_DELETE</ref>
			<ref type="function">REDIS_EXISTS</ref>
			<ref type="function">REDIS_COMMAND</ref>
		</see-also>
	</function>
	<function name="REDIS_EXISTS" language="en_US">
		<synopsis>
			Check to see if a key exists in the Redis database.
		</synopsis>
		<syntax>
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>This function will check to see if a key exists in the Redis
			database. If it exists, the function will return <literal>1</literal>. If not,
			it will return <literal>0</literal>.  Checking for existence of a database key will
			also set the variable REDIS_RESULT to the key's value if it exists.</para>
		</description>
		<see-also>
			<ref type="function">REDIS</ref>
		</see-also>
	</function>
	<function name="REDIS_DELETE" language="en_US">
		<synopsis>
			Return a value from the database and delete it.
		</synopsis>
		<syntax>
			<parameter name="key" required="true" />
		</syntax>
		<description>
			<para>This function will retrieve a value from the Redis database
			and then remove that key from the database.</para>
		</description>
	</function>
	<function name="REDIS_COMMAND" language="en_US">
		<synopsis>
			Send an arbitrary command to Redis.
		</synopsis>
		<syntax>
			<parameter name="command" required="true" />
		</syntax>
		<description>
			<para>Send an arbitrary command to Redis; all Redis commands are valid.
			Words are split on whitespace, double quotes can be used for arguments
			containing spaces. The result is saved in the REDIS_RESULT channel
			variable and returned as the value of the function. Examples:</para>
			<para>REDIS_COMMAND(SET key value)</para>
			<para>REDIS_COMMAND(MGET key1 key2 key3)</para>
			<para>REDIS_COMMAND(LPOP alist)</para>
			<para>REDIS_COMMAND(PUBLISH channel "some message")</para>
		</description>
		<see-also>
			<ref type="function">REDIS</ref>
			<ref type="function">REDIS_DELETE</ref>
			<ref type="function">REDIS_EXISTS</ref>
		</see-also>
	</function>
 ***/

#define REDIS_CONF              "func_redis.conf"
#define STR_CONF_SZ             256
#define REDIS_MAX_ARGS          64
#define REDIS_DEFAULT_HOST      "127.0.0.1"
#define REDIS_DEFAULT_PORT      6379
#define REDIS_DEFAULT_DB        "0"
#define REDIS_DEFAULT_TIMEOUT   5

AST_MUTEX_DEFINE_STATIC(redis_lock);

static char hostname[STR_CONF_SZ] = REDIS_DEFAULT_HOST;
static char dbname[STR_CONF_SZ] = REDIS_DEFAULT_DB;
static char password[STR_CONF_SZ] = "";
static unsigned int port = REDIS_DEFAULT_PORT;
static struct timeval timeout = { REDIS_DEFAULT_TIMEOUT, 0 };
static int bgsave_enabled = 0;

/*! Per-thread Redis connection holder. */
struct redis_conn {
	redisContext *ctx;
};

static int redis_connect(void *data);
static void redis_disconnect(void *data);

AST_THREADSTORAGE_CUSTOM(redis_instance, redis_connect, redis_disconnect);

/*!
 * \brief Connect, authenticate and select the configured database.
 *
 * Mirrors the ast_threadstorage custom init contract: returns 0 on success,
 * non-zero on failure (the buffer is then freed by the caller, so any
 * partially created context must be released here).
 */
static int redis_connect(void *data)
{
	struct redis_conn *conn = data;
	redisContext *ctx;
	redisReply *reply;
	char host[STR_CONF_SZ];
	char pass[STR_CONF_SZ];
	char db[STR_CONF_SZ];
	struct timeval tv;
	unsigned int sport;

	ast_mutex_lock(&redis_lock);
	ast_copy_string(host, hostname, sizeof(host));
	ast_copy_string(pass, password, sizeof(pass));
	ast_copy_string(db, dbname, sizeof(db));
	tv = timeout;
	sport = port;
	ast_mutex_unlock(&redis_lock);

	ctx = redisConnectWithTimeout(host, (int)sport, tv);
	if (!ctx || ctx->err) {
		ast_log(AST_LOG_ERROR, "Could not connect to redis at %s:%u. Reason: %s\n",
			host, sport, ctx ? ctx->errstr : "unknown");
		if (ctx) {
			redisFree(ctx);
		}
		return -1;
	}

	if (!ast_strlen_zero(pass)) {
		ast_debug(1, "REDIS: authenticating...\n");
		reply = redisCommand(ctx, "AUTH %s", pass);
		if (!reply || reply->type == REDIS_REPLY_ERROR) {
			ast_log(AST_LOG_ERROR, "Redis authentication failed. Reason: %s\n",
				reply ? reply->str : ctx->errstr);
			if (reply) {
				freeReplyObject(reply);
			}
			redisFree(ctx);
			return -1;
		}
		freeReplyObject(reply);
		ast_debug(1, "REDIS: authenticated.\n");
	}

	if (!ast_strlen_zero(db)) {
		ast_debug(1, "REDIS: selecting database %s\n", db);
		reply = redisCommand(ctx, "SELECT %s", db);
		if (!reply || reply->type == REDIS_REPLY_ERROR) {
			ast_log(AST_LOG_ERROR, "Unable to select DB %s. Reason: %s\n",
				db, reply ? reply->str : ctx->errstr);
			if (reply) {
				freeReplyObject(reply);
			}
			redisFree(ctx);
			return -1;
		}
		freeReplyObject(reply);
		ast_debug(1, "REDIS: database %s selected.\n", db);
	}

	conn->ctx = ctx;
	return 0;
}

/*!
 * \brief Destructor for the per-thread connection (pthread key destructor).
 *
 * Must release both the Redis context (via the public redisFree() API, the
 * only teardown that stays compatible with hiredis 1.x) and the storage
 * buffer itself.
 */
static void redis_disconnect(void *data)
{
	struct redis_conn *conn = data;

	if (conn && conn->ctx) {
		if (bgsave_enabled) {
			redisReply *reply = redisCommand(conn->ctx, "BGSAVE");
			if (reply) {
				freeReplyObject(reply);
			}
		}
		redisFree(conn->ctx);
		conn->ctx = NULL;
	}
	ast_free(data);
}

/*!
 * \brief Get a working per-thread Redis connection, (re)connecting if needed.
 */
static redisContext *redis_conn_get(void)
{
	struct redis_conn *conn = ast_threadstorage_get(&redis_instance, sizeof(*conn));

	if (!conn) {
		ast_log(AST_LOG_ERROR, "Error allocating redis thread storage\n");
		return NULL;
	}

	if (conn->ctx && conn->ctx->err) {
		ast_debug(1, "REDIS: connection lost (%s), reconnecting\n", conn->ctx->errstr);
		redisFree(conn->ctx);
		conn->ctx = NULL;
	}

	if (!conn->ctx && redis_connect(conn) != 0) {
		return NULL;
	}

	return conn->ctx;
}

/*!
 * \brief Log and run a Redis command.
 *
 * \note The format string must use only %s specifiers (hiredis format), so it
 * can safely be reused for the debug log with printf semantics.
 */
static redisReply *redis_cmd(redisContext *ctx, const char *fmt, ...)
{
	redisReply *reply;
	char *logged = NULL;
	va_list ap, ap2;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	if (ast_vasprintf(&logged, fmt, ap2) >= 0) {
		ast_debug(1, "REDIS: %s\n", logged);
		ast_free(logged);
	}
	va_end(ap2);

	reply = redisvCommand(ctx, fmt, ap);
	va_end(ap);

	return reply;
}

/*!
 * \brief Split a raw command line into argv, honoring double quotes.
 * The input string is modified in place.
 *
 * \retval argc on success
 * \retval -1 if there are more arguments than maxargs
 */
static int redis_command_tokenize(char *cmdline, char **argv, int maxargs)
{
	char *p = cmdline;
	int argc = 0;

	while (*p) {
		while (isspace((unsigned char)*p)) {
			p++;
		}
		if (!*p) {
			break;
		}
		if (argc >= maxargs) {
			return -1;
		}
		if (*p == '"') {
			p++;
			argv[argc++] = p;
			while (*p && *p != '"') {
				p++;
			}
			if (*p == '"') {
				*p++ = '\0';
			}
		} else {
			argv[argc++] = p;
			while (*p && !isspace((unsigned char)*p)) {
				p++;
			}
			if (*p) {
				*p++ = '\0';
			}
		}
	}

	return argc;
}

/*!
 * \brief Run a raw user supplied command line via redisCommandArgv, so that
 * the user input is never interpreted as a hiredis format string.
 */
static redisReply *redis_raw_command(redisContext *ctx, const char *cmdline)
{
	char *cmdline_copy;
	char *argv[REDIS_MAX_ARGS];
	int argc;
	redisReply *reply = NULL;

	ast_debug(1, "REDIS: %s\n", cmdline);

	cmdline_copy = ast_strdup(cmdline);
	if (!cmdline_copy) {
		return NULL;
	}

	argc = redis_command_tokenize(cmdline_copy, argv, REDIS_MAX_ARGS);
	if (argc > 0) {
		reply = redisCommandArgv(ctx, argc, (const char **)argv, NULL);
	}

	ast_free(cmdline_copy);
	return reply;
}

/*!
 * \brief Append the string representation of a redis reply to an ast_str.
 *
 * Redis arrays become comma separated lists. Redis nil becomes "nil".
 * Nested arrays are flattened recursively.
 */
static void reply_append_str(struct ast_str **buf, const redisReply *reply)
{
	size_t i;

	if (!reply) {
		return;
	}

	switch (reply->type) {
	case REDIS_REPLY_NIL:
		ast_str_append(buf, 0, "nil");
		break;
	case REDIS_REPLY_INTEGER:
		ast_str_append(buf, 0, "%lld", reply->integer);
		break;
	case REDIS_REPLY_STRING:
	case REDIS_REPLY_STATUS:
	case REDIS_REPLY_ERROR:
		ast_str_append(buf, 0, "%.*s", (int)reply->len, reply->str);
		break;
#if defined(HIREDIS_MAJOR) && (HIREDIS_MAJOR > 0)
	case REDIS_REPLY_DOUBLE:
		if (reply->str) {
			ast_str_append(buf, 0, "%s", reply->str);
		}
		break;
	case REDIS_REPLY_BOOL:
		ast_str_append(buf, 0, "%lld", reply->integer);
		break;
#endif
	case REDIS_REPLY_ARRAY:
#if defined(HIREDIS_MAJOR) && (HIREDIS_MAJOR > 0)
	case REDIS_REPLY_MAP:
	case REDIS_REPLY_SET:
	case REDIS_REPLY_PUSH:
#endif
		for (i = 0; i < reply->elements; i++) {
			if (i) {
				ast_str_append(buf, 0, ",");
			}
			reply_append_str(buf, reply->element[i]);
		}
		break;
	default:
		ast_str_append(buf, 0, "unknown");
		break;
	}
}

/*!
 * \brief Return the string representation of a redis reply as a new
 * ast_malloc'ed string, or NULL on allocation failure.
 */
static char *reply_value_as_str(const redisReply *reply)
{
	struct ast_str *buf;
	char *result;

	if (!reply) {
		return NULL;
	}

	buf = ast_str_create(64);
	if (!buf) {
		return NULL;
	}

	reply_append_str(&buf, reply);
	result = ast_strdup(ast_str_buffer(buf));
	ast_free(buf);

	return result;
}

static int redis_reply_is_error(const redisReply *reply)
{
	return reply != NULL && reply->type == REDIS_REPLY_ERROR;
}

/*!
 * \brief Handle a NULL reply (connection level error) from redis.
 */
static int redis_report_conn_error(struct ast_channel *chan, redisContext *ctx)
{
	const char *errstr = (ctx && ctx->errstr[0]) ? ctx->errstr : "unknown connection error";

	ast_log(AST_LOG_ERROR, "Redis connection error: %s\n", errstr);
	pbx_builtin_setvar_helper(chan, "REDIS_ERROR", errstr);
	return -1;
}

static int function_redis_read(struct ast_channel *chan, const char *cmd,
	char *parse, char *return_buffer, size_t rtn_buff_len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(key);
	);
	redisReply *reply;
	redisContext *ctx;
	char *value;

	return_buffer[0] = '\0';

	if (ast_strlen_zero(parse)) {
		ast_log(AST_LOG_WARNING, "REDIS requires one argument, REDIS(<key>)\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 1) {
		ast_log(AST_LOG_WARNING, "REDIS requires one argument, REDIS(<key>)\n");
		return -1;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", "connection failed");
		return -1;
	}

	reply = redis_cmd(ctx, "GET %s", args.key);
	if (!reply) {
		return redis_report_conn_error(chan, ctx);
	}
	if (redis_reply_is_error(reply)) {
		ast_log(AST_LOG_ERROR, "Redis GET failed: %s\n", reply->str);
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", reply->str);
		freeReplyObject(reply);
		return -1;
	}

	value = reply_value_as_str(reply);
	freeReplyObject(reply);
	if (value) {
		ast_copy_string(return_buffer, value, rtn_buff_len);
		pbx_builtin_setvar_helper(chan, "REDIS_RESULT", value);
		ast_free(value);
	}

	return 0;
}

static int function_redis_write(struct ast_channel *chan, const char *cmd, char *parse,
	const char *value)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(key);
	);
	redisReply *reply;
	redisContext *ctx;

	if (ast_strlen_zero(parse)) {
		ast_log(AST_LOG_WARNING, "REDIS requires an argument, REDIS(<key>)=<value>\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 1) {
		ast_log(AST_LOG_WARNING, "REDIS requires one argument, REDIS(<key>)=<value>\n");
		return -1;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", "connection failed");
		return -1;
	}

	reply = redis_cmd(ctx, "SET %s %s", args.key, value ? value : "");
	if (!reply) {
		return redis_report_conn_error(chan, ctx);
	}
	if (redis_reply_is_error(reply)) {
		ast_log(AST_LOG_WARNING, "REDIS: error writing value to database. Reason: %s\n", reply->str);
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", reply->str);
		freeReplyObject(reply);
		return -1;
	}

	freeReplyObject(reply);
	return 0;
}

static struct ast_custom_function redis_function = {
	.name = "REDIS",
	.read = function_redis_read,
	.write = function_redis_write,
};

static int function_redis_exists(struct ast_channel *chan, const char *cmd,
	char *parse, char *return_buffer, size_t rtn_buff_len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(key);
	);
	redisReply *reply;
	redisContext *ctx;

	return_buffer[0] = '\0';

	if (ast_strlen_zero(parse)) {
		ast_log(AST_LOG_WARNING, "REDIS_EXISTS requires one argument, REDIS_EXISTS(<key>)\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 1) {
		ast_log(AST_LOG_WARNING, "REDIS_EXISTS requires one argument, REDIS_EXISTS(<key>)\n");
		return -1;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", "connection failed");
		return -1;
	}

	reply = redis_cmd(ctx, "EXISTS %s", args.key);
	if (!reply) {
		return redis_report_conn_error(chan, ctx);
	}
	if (redis_reply_is_error(reply)) {
		ast_log(AST_LOG_ERROR, "Redis EXISTS failed: %s\n", reply->str);
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", reply->str);
		freeReplyObject(reply);
		return -1;
	}

	ast_copy_string(return_buffer, reply->integer ? "1" : "0", rtn_buff_len);
	freeReplyObject(reply);
	pbx_builtin_setvar_helper(chan, "REDIS_RESULT", return_buffer);

	return 0;
}

static struct ast_custom_function redis_exists_function = {
	.name = "REDIS_EXISTS",
	.read = function_redis_exists,
};

static int function_redis_delete(struct ast_channel *chan, const char *cmd,
	char *parse, char *return_buffer, size_t rtn_buff_len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(key);
	);
	redisReply *reply;
	redisContext *ctx;

	return_buffer[0] = '\0';

	if (ast_strlen_zero(parse)) {
		ast_log(AST_LOG_WARNING, "REDIS_DELETE requires one argument, REDIS_DELETE(<key>)\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 1) {
		ast_log(AST_LOG_WARNING, "REDIS_DELETE requires one argument, REDIS_DELETE(<key>)\n");
		return -1;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", "connection failed");
		return -1;
	}

	reply = redis_cmd(ctx, "DEL %s", args.key);
	if (!reply) {
		return redis_report_conn_error(chan, ctx);
	}
	if (redis_reply_is_error(reply)) {
		ast_log(AST_LOG_ERROR, "Redis DEL failed: %s\n", reply->str);
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", reply->str);
		freeReplyObject(reply);
		return -1;
	}

	if (reply->integer == 0) {
		ast_log(AST_LOG_WARNING, "REDIS_DELETE: key %s not found in database.\n", args.key);
	}

	freeReplyObject(reply);
	return 0;
}

/*!
 * \brief Wrapper to execute REDIS_DELETE from a write operation. Allows execution
 * even if live_dangerously is disabled.
 */
static int function_redis_delete_write(struct ast_channel *chan, const char *cmd, char *parse,
	const char *value)
{
	/* Throwaway to hold the result from the read */
	char return_buffer[128];
	return function_redis_delete(chan, cmd, parse, return_buffer, sizeof(return_buffer));
}

static struct ast_custom_function redis_delete_function = {
	.name = "REDIS_DELETE",
	.read = function_redis_delete,
	.write = function_redis_delete_write,
};

static int function_redis_command(struct ast_channel *chan, const char *cmd,
	char *parse, char *return_buffer, size_t rtn_buff_len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(command);
	);
	redisReply *reply;
	redisContext *ctx;
	char *value;

	return_buffer[0] = '\0';

	if (ast_strlen_zero(parse)) {
		ast_log(AST_LOG_WARNING, "REDIS_COMMAND requires one argument, REDIS_COMMAND(<command>)\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 1) {
		ast_log(AST_LOG_WARNING, "REDIS_COMMAND requires one argument, REDIS_COMMAND(<command>)\n");
		return -1;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", "connection failed");
		return -1;
	}

	reply = redis_raw_command(ctx, args.command);
	if (!reply) {
		return redis_report_conn_error(chan, ctx);
	}
	if (redis_reply_is_error(reply)) {
		ast_log(AST_LOG_ERROR, "Redis command failed: %s\n", reply->str);
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", reply->str);
		freeReplyObject(reply);
		return -1;
	}

	value = reply_value_as_str(reply);
	freeReplyObject(reply);
	if (value) {
		ast_copy_string(return_buffer, value, rtn_buff_len);
		ast_free(value);
	} else {
		pbx_builtin_setvar_helper(chan, "REDIS_ERROR", "error formatting redis reply");
		return -1;
	}
	pbx_builtin_setvar_helper(chan, "REDIS_RESULT", return_buffer);

	return 0;
}

static struct ast_custom_function redis_command_function = {
	.name = "REDIS_COMMAND",
	.read = function_redis_command,
};

static int load_config(void)
{
	struct ast_config *config;
	const char *conf_str;
	struct ast_flags config_flags = { 0 };
	char host[STR_CONF_SZ] = REDIS_DEFAULT_HOST;
	char db[STR_CONF_SZ] = REDIS_DEFAULT_DB;
	char pass[STR_CONF_SZ] = "";
	uint32_t cfg_port = REDIS_DEFAULT_PORT;
	struct timeval cfg_timeout = { REDIS_DEFAULT_TIMEOUT, 0 };
	int cfg_bgsave = 0;

	config = ast_config_load(REDIS_CONF, config_flags);
	if (!config || config == CONFIG_STATUS_FILEINVALID) {
		ast_log(AST_LOG_ERROR, "Unable to load config %s\n", REDIS_CONF);
		return -1;
	}

	if ((conf_str = ast_variable_retrieve(config, "general", "hostname")) && !ast_strlen_zero(conf_str)) {
		ast_copy_string(host, conf_str, sizeof(host));
	} else {
		ast_log(AST_LOG_NOTICE, "No redis hostname configured, using %s as default.\n", host);
	}

	if ((conf_str = ast_variable_retrieve(config, "general", "port")) && !ast_strlen_zero(conf_str)) {
		if (ast_parse_arg(conf_str, PARSE_UINT32 | PARSE_IN_RANGE | PARSE_DEFAULT,
				&cfg_port, (uint32_t)REDIS_DEFAULT_PORT, 1, 65535)) {
			ast_log(AST_LOG_WARNING, "Invalid redis port '%s', using %u.\n",
				conf_str, (unsigned int)REDIS_DEFAULT_PORT);
		}
	} else {
		ast_log(AST_LOG_NOTICE, "No redis port configured, using %u as default.\n",
			(unsigned int)REDIS_DEFAULT_PORT);
	}

	if ((conf_str = ast_variable_retrieve(config, "general", "database")) && !ast_strlen_zero(conf_str)) {
		ast_copy_string(db, conf_str, sizeof(db));
	} else {
		ast_log(AST_LOG_NOTICE, "No redis database configured, using %s as default.\n", db);
	}

	if ((conf_str = ast_variable_retrieve(config, "general", "password")) && !ast_strlen_zero(conf_str)) {
		ast_copy_string(pass, conf_str, sizeof(pass));
	}

	if ((conf_str = ast_variable_retrieve(config, "general", "timeout")) && !ast_strlen_zero(conf_str)) {
		uint32_t secs = REDIS_DEFAULT_TIMEOUT;
		if (ast_parse_arg(conf_str, PARSE_UINT32 | PARSE_IN_RANGE | PARSE_DEFAULT,
				&secs, (uint32_t)REDIS_DEFAULT_TIMEOUT, 1, 600)) {
			ast_log(AST_LOG_WARNING, "Invalid redis timeout '%s', using %u seconds.\n",
				conf_str, (unsigned int)REDIS_DEFAULT_TIMEOUT);
		}
		cfg_timeout.tv_sec = secs;
	} else {
		ast_log(AST_LOG_NOTICE, "No redis timeout configured, using %u seconds as default.\n",
			(unsigned int)REDIS_DEFAULT_TIMEOUT);
	}

	if ((conf_str = ast_variable_retrieve(config, "general", "bgsave")) && !ast_strlen_zero(conf_str)) {
		cfg_bgsave = ast_true(conf_str);
	}

	ast_config_destroy(config);

	ast_mutex_lock(&redis_lock);
	ast_copy_string(hostname, host, sizeof(hostname));
	ast_copy_string(dbname, db, sizeof(dbname));
	ast_copy_string(password, pass, sizeof(password));
	port = cfg_port;
	timeout = cfg_timeout;
	bgsave_enabled = cfg_bgsave;
	ast_mutex_unlock(&redis_lock);

	ast_verb(2, "Redis config loaded.\n");

	return 0;
}

static char *handle_cli_redis_set(struct ast_cli_entry *e, int cmd, struct ast_cli_args *args)
{
	redisReply *reply;
	redisContext *ctx;

	switch (cmd) {
	case CLI_INIT:
		e->command = "redis set";
		e->usage =
			"Usage: redis set <key> <value>\n"
			"       Creates an entry in the Redis database for a given key and value.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (args->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		ast_cli(args->fd, "Redis connection error.\n");
		return CLI_FAILURE;
	}

	reply = redis_cmd(ctx, "SET %s %s", args->argv[2], args->argv[3]);
	if (!reply) {
		ast_cli(args->fd, "Redis connection error: %s\n", ctx->errstr);
	} else if (redis_reply_is_error(reply)) {
		ast_cli(args->fd, "%s\n", reply->str);
		ast_cli(args->fd, "Redis database error.\n");
	} else {
		ast_cli(args->fd, "Redis database entry created.\n");
	}
	if (reply) {
		freeReplyObject(reply);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_redis_del(struct ast_cli_entry *e, int cmd, struct ast_cli_args *args)
{
	redisReply *reply;
	redisContext *ctx;

	switch (cmd) {
	case CLI_INIT:
		e->command = "redis del";
		e->usage =
			"Usage: redis del <key>\n"
			"       Deletes an entry in the Redis database for a given key.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	if (args->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		ast_cli(args->fd, "Redis connection error.\n");
		return CLI_FAILURE;
	}

	reply = redis_cmd(ctx, "DEL %s", args->argv[2]);
	if (!reply) {
		ast_cli(args->fd, "Redis connection error: %s\n", ctx->errstr);
	} else if (redis_reply_is_error(reply)) {
		ast_cli(args->fd, "%s\n", reply->str);
		ast_cli(args->fd, "Redis database error.\n");
	} else if (reply->integer == 0) {
		ast_cli(args->fd, "Redis database entry does not exist.\n");
	} else {
		ast_cli(args->fd, "Redis database entry removed.\n");
	}
	if (reply) {
		freeReplyObject(reply);
	}
	return CLI_SUCCESS;
}

static char *handle_cli_redis_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *args)
{
	redisReply *reply;
	redisReply *get_reply;
	redisContext *ctx;
	unsigned int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "redis show";
		e->usage =
			"Usage: redis show\n"
			"   OR: redis show [pattern]\n"
			"       Shows Redis database contents, optionally restricted\n"
			"       to a pattern.\n"
			"\n"
			"       [pattern] pattern to match keys\n"
			"       Examples :\n"
			"           - h?llo matches hello, hallo and hxllo\n"
			"           - h*llo matches hllo and heeeello\n"
			"           - h[ae]llo matches hello and hallo, but not hillo\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	default:
		break;
	}

	ctx = redis_conn_get();
	if (!ctx) {
		ast_cli(args->fd, "Redis connection error.\n");
		return CLI_FAILURE;
	}

	if (args->argc == 3) {
		/* key pattern */
		reply = redis_cmd(ctx, "KEYS %s", args->argv[2]);
	} else if (args->argc == 2) {
		/* show all */
		reply = redis_cmd(ctx, "KEYS *");
	} else {
		return CLI_SHOWUSAGE;
	}

	if (!reply) {
		ast_cli(args->fd, "Redis connection error: %s\n", ctx->errstr);
		return CLI_FAILURE;
	}
	if (redis_reply_is_error(reply)) {
		ast_cli(args->fd, "%s\n", reply->str);
		freeReplyObject(reply);
		return CLI_FAILURE;
	}

	for (i = 0; i < reply->elements; i++) {
		get_reply = redis_cmd(ctx, "GET %s", reply->element[i]->str);
		if (redis_reply_is_error(get_reply)) {
			ast_cli(args->fd, "%-50s: <error: %s>\n", reply->element[i]->str, get_reply->str);
		} else {
			char *value = reply_value_as_str(get_reply);
			if (value) {
				ast_cli(args->fd, "%-50s: %-25s\n", reply->element[i]->str, value);
				ast_free(value);
			}
		}
		if (get_reply) {
			freeReplyObject(get_reply);
		}
	}

	ast_cli(args->fd, "%d results found.\n", (int)reply->elements);
	freeReplyObject(reply);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_func_redis[] = {
	AST_CLI_DEFINE(handle_cli_redis_show, "Get all Redis values or by pattern in key"),
	AST_CLI_DEFINE(handle_cli_redis_del, "Delete a key - value in Redis"),
	AST_CLI_DEFINE(handle_cli_redis_set, "Creates a new key - value in Redis"),
};

static int unload_module(void)
{
	int res = 0;

	if (bgsave_enabled) {
		redisContext *ctx = redis_conn_get();
		if (ctx) {
			redisReply *reply = redis_cmd(ctx, "BGSAVE");
			if (reply) {
				ast_log(AST_LOG_NOTICE, "BGSAVE sent before closing connection.\n");
				freeReplyObject(reply);
			}
		}
	}

	ast_cli_unregister_multiple(cli_func_redis, ARRAY_LEN(cli_func_redis));
	res |= ast_custom_function_unregister(&redis_function);
	res |= ast_custom_function_unregister(&redis_exists_function);
	res |= ast_custom_function_unregister(&redis_delete_function);
	res |= ast_custom_function_unregister(&redis_command_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	if (load_config() != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(cli_func_redis, ARRAY_LEN(cli_func_redis));

	res |= ast_custom_function_register(&redis_function);
	res |= ast_custom_function_register(&redis_exists_function);
	res |= ast_custom_function_register(&redis_delete_function);
	res |= ast_custom_function_register(&redis_command_function);

	if (res) {
		ast_cli_unregister_multiple(cli_func_redis, ARRAY_LEN(cli_func_redis));
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	ast_log(AST_LOG_NOTICE, "Reloading.\n");
	return load_config();
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Redis related dialplan functions",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_DEFAULT,
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
);
