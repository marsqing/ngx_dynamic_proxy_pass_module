-- uid = get_cookie("UID");
uid = get_ngx_http_variable("cookie_uid");
ups = {get_upstream_list()};

ups_cnt = #ups / 2;
i = 2;
bucket_cnt = 0;
while i <= #ups do
	bucket_cnt = bucket_cnt + ups[i]	
	i = i + 2;
end
modus = uid % bucket_cnt;
if modus < ups[2] then
	upstream = ups[1];
else
	upstream = ups[3];
end

f=io.open("/Users/marsqing/Projects/tmp/luajit/log", "aw");
f:write(string.format("%d %d %d\n", uid, bucket_cnt, modus));
i = 1;
while i <= #ups do
	f:write(string.format("%s\n", ups[i]));
	i = i + 1;
end
f:write(string.format("arg_name=%s\n", get_ngx_http_variable("arg_name")));
f:close();

-- upstream = "six@0";
