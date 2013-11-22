#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "ngx_http_dyups_module.h"

typedef struct {
	ngx_str_t dp_domain;
	ngx_str_t lb_lua_file;
	lua_State *L;
} ngx_http_dypp_loc_conf_t;

ngx_http_request_t *cur_r;
ngx_str_t *cur_dp_domain;

static ngx_command_t ngx_dynamic_proxy_pass_module_commands[] = {
	{
		ngx_string("dp_domain"), // The command name
		NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_str_slot, // The command handler
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_dypp_loc_conf_t, dp_domain),
		NULL
	},

	{ ngx_string("lb_lua_file"),
		NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_dypp_loc_conf_t, lb_lua_file),
		NULL },


	ngx_null_command
};

static ngx_int_t ngx_http_dypp_postconfig(ngx_conf_t *cf);

static void* ngx_http_dypp_create_loc_conf(ngx_conf_t* cf);

static char* ngx_http_dypp_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_int_t ngx_http_dypp_preconfig(ngx_conf_t *cf);

ngx_int_t ngx_http_dypp_get_variable (ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);

static u_char* call_lua(ngx_http_request_t *r, lua_State *L);

static int get_cookie(lua_State *L) ;

static int get_upstream_list(lua_State *L) ;

static int get_ngx_http_variable(lua_State *L);

static ngx_http_module_t ngx_dynamic_proxy_pass_module_ctx = {
	ngx_http_dypp_preconfig,
	ngx_http_dypp_postconfig,
	NULL,
	NULL,
	NULL,
	NULL,
	ngx_http_dypp_create_loc_conf,
	ngx_http_dypp_merge_loc_conf
};

ngx_module_t ngx_dynamic_proxy_pass_module = {
	NGX_MODULE_V1,
	&ngx_dynamic_proxy_pass_module_ctx,
	ngx_dynamic_proxy_pass_module_commands,
	NGX_HTTP_MODULE,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NGX_MODULE_V1_PADDING
};


//ngx_int_t    
//ngx_http_dypp_init(ngx_cycle_t *cycle){
//	L = luaL_newstate();
//	if(L == NULL) {
//		ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Can not init lua");
//		return NGX_ERROR;
//	}
//	luaL_openlibs(L);
//	lua_register(L, "get_cookie", get_cookie);
//	lua_register(L, "get_upstream_list", get_upstream_list);
//	lua_register(L, "get_ngx_http_variable", get_ngx_http_variable);
////	lua_pushcfunction(L, get_cookie);
////	lua_setglobal(L, "get_cookie");
////	lua_pushcfunction(L, get_upstream_list);
////	lua_setglobal(L, "get_upstream_list");
////	lua_pushcfunction(L, get_ngx_http_variable);
////	lua_setglobal(L, "get_ngx_http_variable");
//	//int error = luaL_loadstring(L, "f=io.open(\"/Users/marsqing/Projects/tmp/luajit/log\", \"w\");f:write(\"success\");f:close();xx=101;") || lua_pcall(L, 0, 0, 0);
//	//int error = luaL_loadfile(L, "/Users/marsqing/Projects/tengine/ngx_dynamic_proxy_pass_module/rule.lua")|| lua_pcall(L, 0, 0, 0);
//	if (luaL_loadfile(L, "/Users/hupeng/loadbalance/tengine/ngx_dynamic_proxy_pass_module/rule.lua") || lua_pcall(L,0,0,0)) {
//		return NGX_ERROR;
//	}
//	//ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "dypp_lua_file %s", dypp_lua_file);
//	//int error = luaL_loadfile(L, "/Users/hupeng/loadbalance/tengine/ngx_dynamic_proxy_pass_module/rule.lua");
//	return NGX_OK;
//}
static void* ngx_http_dypp_create_loc_conf(ngx_conf_t* cf) {
	ngx_http_dypp_loc_conf_t* conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_dypp_loc_conf_t));
	if (conf == NULL) {
		return NGX_CONF_ERROR;
	}
	conf->dp_domain.len = 0;
	conf->dp_domain.data = NULL;
	conf->lb_lua_file.data = NULL;
	conf->lb_lua_file.len = 0;

	return conf;
}

static char* ngx_http_dypp_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child){
	ngx_http_dypp_loc_conf_t *prev = parent;
	ngx_http_dypp_loc_conf_t *conf = child;
	
	ngx_conf_merge_str_value(conf->dp_domain, prev->dp_domain, "");
	ngx_conf_merge_str_value(conf->lb_lua_file, prev->lb_lua_file, "");
	
	return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_dypp_preconfig(ngx_conf_t *cf){
	ngx_http_variable_t           *var;
	ngx_str_t name;

	char* char_name = "dp_upstream";
	name.len = strlen(char_name);
	name.data = ngx_pcalloc(cf->pool, name.len);
	ngx_memcpy(name.data, char_name, name.len);

	var = ngx_http_add_variable(cf, &name, NGX_HTTP_VAR_CHANGEABLE);
	if (var == NULL) {
		return NGX_ERROR;
	}
	//设置回调
	var->get_handler = ngx_http_dypp_get_variable;
	//var->data = (uintptr_t) ctx;
	return NGX_OK;
}

static int get_cookie(lua_State *L) {
	ngx_str_t cookie_name;
	cookie_name.data = (u_char*)lua_tolstring(L, 1, &cookie_name.len);
	ngx_str_t *value = ngx_pnalloc(cur_r->pool, sizeof(ngx_str_t));
	ngx_http_parse_multi_header_lines(&cur_r->headers_in.cookies, &cookie_name, value);
	lua_pushlstring(L, (char*)value->data, value->len);
	ngx_log_debug1(NGX_LOG_DEBUG, cur_r->connection->log, 0, "parameter from lua %V", &cookie_name);
	return 1;	
}

static int get_ngx_http_variable(lua_State *L) {
	u_char *p, *lowcase;
	size_t len;
	ngx_uint_t hash;
	ngx_str_t name;
	ngx_http_variable_value_t *vv;

	p = (u_char*)lua_tolstring(L, 1, &len);
	lowcase = ngx_pnalloc(cur_r->pool, len);
	hash = ngx_hash_strlow(lowcase, p, len);

	name.len = len;
	name.data = lowcase;
	vv = ngx_http_get_variable(cur_r, &name, hash);

	if(vv && vv->len > 0 && vv->data && vv->not_found != 1) {
		lua_pushlstring(L, (char*)vv->data, vv->len);
		return 1;
	} else {
		char buf[2];
		buf[0] = '0';
		buf[1] = '\0';
		lua_pushlstring(L, buf, 1);
		return 1;
	}
}

extern ngx_module_t  ngx_http_dyups_module;
static int get_upstream_list(lua_State *L) {
	ngx_http_dyups_main_conf_t  *dumcf;
	dumcf = ngx_http_get_module_main_conf(cur_r, ngx_http_dyups_module);

	ngx_uint_t len, i, chosen_upstream_cnt;
        len = 0;
	chosen_upstream_cnt = 0;
	ngx_http_dyups_srv_conf_t *duscfs, *duscf;
	duscfs = dumcf->dy_upstreams.elts;
	for (i = 0; i < dumcf->dy_upstreams.nelts; i++) {
		duscf = &duscfs[i];

		ngx_str_t *upstream_name = &duscf->upstream->host;
		if(ngx_strncmp(upstream_name->data, cur_dp_domain->data, cur_dp_domain->len) == 0) {
			if(upstream_name->data[cur_dp_domain->len] == '@') {
				chosen_upstream_cnt++;
				lua_pushlstring(L, (char*)duscf->upstream->host.data, duscf->upstream->host.len);
				lua_pushinteger(L, duscf->upstream->servers->nelts);
				/*
				ngx_uint_t j;
				ngx_http_upstream_server_t *us;
				ngx_log_error(NGX_LOG_ERR, cur_r->connection->log, 0, "DYNAMIC %V", &duscf->upstream->host);
				us = duscf->upstream->servers->elts;
				for (j = 0; j < duscf->upstream->servers->nelts; j++) {
				    ngx_log_error(NGX_LOG_ERR, cur_r->connection->log, 0, "DYNAMIC server %V", &us[j].addrs->name);
				}
				*/
			}
		}

	}


	//lua_pushstring(L, "user-web_20130909");
	//lua_pushinteger(L, 2);
	//lua_pushstring(L, "user-web_20130910");
	//lua_pushinteger(L, 4);
	return 2 * chosen_upstream_cnt;
}

static ngx_int_t ngx_http_dypp_postconfig(ngx_conf_t *cf){
	// attach module handler
	/*
	ngx_http_handler_pt        *h;
	ngx_http_core_main_conf_t  *cmcf;
	cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
	h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);
	if (h == NULL) {
		return NGX_ERROR;
	}
	*h = ngx_http_hello_world_handler;
	*/
	ngx_http_dypp_loc_conf_t *conf;

	conf = ngx_http_conf_get_module_loc_conf(cf, ngx_dynamic_proxy_pass_module);
//	ngx_log_debug(NGX_LOG_DEBUG_HTTP, cf->log, 0, "INIT lua, %s", (char*)conf->lua_file.data);
//								
//	dypp_lua_file = (char*)conf->lua_file.data;

	return NGX_OK;
}


static u_char* call_lua(ngx_http_request_t *r, lua_State *L) {
	cur_r = r;
	lua_getglobal(L, "choose_upstream");
	lua_pcall(L, 0, 0, 0);
	// TODO can we use lua_tostring(L, -1) and is it faster?
	lua_getglobal(L, "upstream");
	const char *lua_result = lua_tostring(L, -1);
	char* chosen_upstream = ngx_pcalloc(r->pool, strlen(lua_result) + 1);
	strcpy(chosen_upstream, lua_result);
	ngx_log_debug1(NGX_LOG_DEBUG, r->connection->log, 0, "[dypp] lua result %s", chosen_upstream);
	//lua_close(L);
	lua_pop(L, 1);
	return (u_char*)chosen_upstream;
}

ngx_int_t ngx_http_dypp_get_variable (ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data){
	ngx_http_dypp_loc_conf_t *hdlc;
	hdlc = ngx_http_get_module_loc_conf(r, ngx_dynamic_proxy_pass_module);
	if(hdlc->L == NULL){
		hdlc->L = luaL_newstate();
		if(hdlc->L == NULL) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Can not init lua");
			return NGX_ERROR;
		}
		luaL_openlibs(hdlc->L);
		lua_register(hdlc->L, "get_cookie", get_cookie);
		lua_register(hdlc->L, "get_upstream_list", get_upstream_list);
		lua_register(hdlc->L, "get_ngx_http_variable", get_ngx_http_variable);
		
		if (luaL_loadfile(hdlc->L, (char*)hdlc->lb_lua_file.data) || lua_pcall(hdlc->L,0,0,0)) {
			return NGX_ERROR;
		}
	
	}
	cur_dp_domain = &hdlc->dp_domain;

	//ngx_str_t baidu = ngx_string("baidu");
	//ngx_str_t dianping = ngx_string("dianping");

	u_char *chosen_upstream = call_lua(r, hdlc->L);
	
	/*
	if(ahlf == NULL) {
		call_lua(r);
	}
	char* chosen_upstream = ngx_pcalloc(r->pool, 6);
	strcpy(chosen_upstream, "six@0");
	*/

	if(r->headers_in.cookies.nelts > 0){

		/*
		ngx_str_t *value = ngx_pnalloc(r->pool, sizeof(ngx_str_t));
		ngx_str_t cookie_name = ngx_string("UID");
		ngx_http_parse_multi_header_lines(&r->headers_in.cookies, &cookie_name, value);
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "UID %V", value);
		*/
	
		/*	
		if (ngx_strncasecmp(value->data, baidu.data, baidu.len) == 0) {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "GOTO BIDU");
			chosen_pool = &baidu;
		} else {
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "GOTO DP");
			chosen_pool = &dianping;
		}
		*/

		/*
		ngx_table_elt_t** cookies = r->headers_in.cookies.elts;
		int i = 0;
		for(i = 0; i < (int)r->headers_in.cookies.nelts; i++){
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "COOKIE %V", &cookies[i]->value);
		}
		*/
	}
	v->len = strlen((char*)chosen_upstream);
	v->data = (u_char*)chosen_upstream;
	/*
	if (v->data == NULL) {
		return NGX_ERROR;
	}
	ngx_memcpy(v->data, chosen_pool->data, chosen_pool->len);
	*/
	v->valid = 1;

	return NGX_OK;
}
