.load ./tinybits
create temp table t(b blob);
insert into t select tb_from_json('{"id":42,"name":"Jane \"J\" é\n","tags":["a","bb","a","bb"],"pi":3.14159,"big":12345678901234,"neg":-7,"ok":true,"no":false,"nil":null,"nested":{"a1":{"x":[1,2,{"y":"deep"}]},"a2":1.5e300},"empty":{},"e2":[]}');
select length(b), tb_valid(b) from t;
select tb_json(b) from t;
select tb_extract(b,'$.id'), tb_extract(b,'$.name'), tb_extract(b,'$.pi'), tb_extract(b,'$.big'), tb_extract(b,'$.neg'), tb_extract(b,'$.ok'), tb_extract(b,'$.nil') is null from t;
select tb_extract(b,'$.tags[3]'), tb_extract(b,'$.nested.a1.x[2].y'), tb_extract(b,'$.nested.a2'), tb_extract(b,'$.missing') is null, tb_extract(b,'$.tags[9]') is null from t;
select tb_json(tb_extract(b,'$.nested.a1')), tb_json(b,'$.tags'), tb_type(b), tb_type(b,'$.tags'), tb_type(b,'$.pi'), tb_type(b,'$.nil'), tb_array_length(b,'$.tags'), tb_array_length(b,'$.id') from t;
select tb_valid(x'0803'), tb_valid(x''), tb_valid(zeroblob(0)), tb_valid(substr(b,1,10)) from t;
select tb_from_json('[1,]');
