# ruby sqlite/bench.rb [rows, default 20000]
# json text vs jsonb vs tinybits inside SQLite. Every measurement is one query running entirely in SQLite.
# One table per format, 64KB pages so no payload spills into overflow pages (that would measure storage, not decoding)
require 'json'
require 'open3'

ROWS = (ARGV[0] || 20_000).to_i
DIR = __dir__
DB = File.join(DIR, "bench.db")
JSON_DIR = File.expand_path("../../tinybits-rb/json", DIR)
DOCS = %w[single/users single/telemetry users telemetry]
EXT = File.join(DIR, "tinybits")

def leaves(obj, path = "$", depth = 0, acc = [])
  case obj
  when Hash then obj.each { |k, v| leaves(v, "#{path}.#{k}", depth + 1, acc) if k.match?(/\A\w+\z/) }
  when Array then obj.each_with_index { |v, i| leaves(v, "#{path}[#{i}]", depth + 1, acc) }
  else acc << [path, depth]
  end
  acc
end

def sh(db, sql, timer: true)
  input = [".load #{EXT}", (".timer on" if timer), sql].compact.join("\n") + "\n"
  out, err, st = Open3.capture3("/usr/bin/time", "-f", "%M", "sqlite3", db, stdin_data: input)
  raise "#{sql}\n#{err}" unless st.success?
  real = out[/real ([\d.]+)/, 1]&.to_f
  [real, err.lines.last.to_i, out.lines.first.to_s.strip]
end

File.delete(DB) if File.exist?(DB)
DOCS.each do |d|
  t = d.tr("/", "_")
  file = File.join(JSON_DIR, "#{d}.json")
  sql = +"pragma page_size=65536;\n"
  sql << "create table src_#{t}(t text); insert into src_#{t} values(trim(readfile('#{file}')));\n"
  # trailing whitespace differs per row so SQLite's JSON parse cache (last few texts) never hits
  pad = "substr('#{' ' * 63}', 1, i % 64)"
  { "j" => ["text", "s.t || #{pad}"], "b" => ["blob", "jsonb(s.t)"], "t" => ["blob", "tb_from_json(s.t)"] }.each do |pre, (type, expr)|
    sql << "create table #{pre}_#{t}(id integer primary key, v #{type});\n"
    sql << "with recursive n(i) as (select 1 union all select i+1 from n where i<#{ROWS}) " \
           "insert into #{pre}_#{t} select i, #{expr} from n, src_#{t} s;\n"
  end
  sh(DB, sql, timer: false)
end

puts "sqlite #{`sqlite3 --version`.split.first}, #{ROWS} identical rows per table, ns/row (lower is better)"
idle = sh(DB, "select 1;")[1]
max_rss = Hash.new(idle)
FMT = { "j" => "json", "b" => "jsonb", "t" => "tinybits" }

def run(db, max_rss, pre, sql)
  real, rss, = sh(db, sql)
  max_rss[pre] = [max_rss[pre], rss].max
  1e9 * real / ROWS
end

DOCS.each do |d|
  t = d.tr("/", "_")
  doc = JSON.parse(File.read(File.join(JSON_DIR, "#{d}.json")))
  ls = leaves(doc)
  paths = { "first" => ls.first[0], "last" => ls.last[0], "deepest" => ls.max_by { |_, dep| dep }[0] }
  sz = %w[j b t].map { |pre| sh(DB, "select length(cast(v as blob)) from #{pre}_#{t} limit 1;", timer: false)[2].to_i }
  puts
  puts "== #{d}: bytes json #{sz[0]}, jsonb #{sz[1]}, tinybits #{sz[2]}"
  puts format("%-34s %12s %12s %12s", "ns/row", "json", "jsonb", "tinybits")
  row = %w[j b t].map { |pre| run(DB, max_rss, pre, "select sum(length(cast(v as blob))) from #{pre}_#{t};") }
  puts format("%-34s %12.0f %12.0f %12.0f", "fetch only (cast v as blob)", *row)
  paths.each do |label, p|
    exprs = { "j" => "json_extract(v,'#{p}')", "b" => "jsonb_extract(v,'#{p}')", "t" => "tb_extract(v,'#{p}')" }
    row = exprs.map { |pre, e| run(DB, max_rss, pre, "select sum(length(#{e})) from #{pre}_#{t};") }
    puts format("%-34s %12.0f %12.0f %12.0f", "extract #{label} #{p}"[0, 34], *row)
  end
  {
    "to JSON text (minify/decode)" => { "j" => "length(json(v))", "b" => "length(json(v))", "t" => "length(tb_json(v))" },
    "from JSON text (parse/encode)" => { "j" => "length(json(v))", "b" => "length(jsonb(v))", "t" => "length(tb_from_json(v))" },
  }.each do |label, exprs|
    row = %w[j b t].map do |pre|
      src = pre == "j" || label.start_with?("from") ? "j_#{t}" : "#{pre}_#{t}"
      run(DB, max_rss, pre, "select sum(#{exprs[pre]}) from #{src};")
    end
    puts format("%-34s %12.0f %12.0f %12.0f", label, *row)
  end
end
puts
puts format("RSS MB  idle %.1f  peak json %.1f  jsonb %.1f  tinybits %.1f", idle / 1024.0,
  max_rss["j"] / 1024.0, max_rss["b"] / 1024.0, max_rss["t"] / 1024.0)
