# ruby -I ../../tinybits-rb/lib sqlite/roundtrip.rb [more.json ...]
# Packs docs with the Ruby gem, then checks tb_json and tb_json(blob, path) for every container/leaf path against Ruby's parse
require 'json'
require 'tempfile'
require 'tinybits'

MAX_PATHS = 400
root = File.expand_path("../../tinybits-rb/json", __dir__)
files = Dir[File.join(root, "*.json"), File.join(root, "single", "*.json")] + ARGV
packer = TinyBits::Packer.new

def paths(obj, path = "$", acc = [])
  acc << [path, obj]
  case obj
  when Hash
    obj.each { |k, v| paths(v, "#{path}[#{JSON.generate(k)}]", acc) unless k.include?('"') || k.include?("\\") }
  when Array
    obj.each_with_index { |v, i| paths(v, "#{path}[#{i}]", acc) }
  end
  acc
end

def same?(a, b)
  case a
  when Float then b.is_a?(Numeric) && (a == b || (a - b).abs <= 1e-12 * a.abs)
  when Hash then b.is_a?(Hash) && a.size == b.size && a.all? { |k, v| b.key?(k) && same?(v, b[k]) }
  when Array then b.is_a?(Array) && a.size == b.size && a.zip(b).all? { |x, y| same?(x, y) }
  else a == b
  end
end

sql = +".load #{File.expand_path('tinybits', __dir__)}\n.mode list\n.separator \"\\t\"\ncreate temp table d(id integer primary key, b blob);\n"
expect = {}
files.each_with_index do |f, fi|
  doc = JSON.parse(File.read(f)) rescue next
  next if File.size(f) > 400_000
  sql << "insert into d values(#{fi}, x'#{packer.pack(doc).unpack1("H*")}');\n"
  all = paths(doc)
  all = all.first(MAX_PATHS) + all.last(MAX_PATHS) if all.size > 2 * MAX_PATHS
  all.each_with_index do |(path, val), pi|
    id = "#{fi}:#{pi}"
    expect[id] = [File.basename(f), path, val]
    sql << "select '#{id}', tb_json(b, '#{path.gsub("'", "''")}') from d where id = #{fi};\n"
  end
  id = "#{fi}:v"
  expect[id] = [File.basename(f), "valid", 1]
  sql << "select '#{id}', tb_valid(b) from d where id = #{fi};\n"
end

out = Tempfile.new("rt")
Tempfile.create("rt_sql") do |t|
  t.write(sql); t.flush
  system("sqlite3 < #{t.path} > #{out.path} 2> #{out.path}.err")
end
got = File.readlines(out.path, chomp: true).to_h { |l| l.split("\t", 2) }
bad = 0
expect.each do |id, (name, path, val)|
  g = got[id]
  ok = if path == "valid" then g == "1"
       else !g.nil? && (same?(val, JSON.parse(g, allow_nan: true)) rescue false)
       end
  next if ok
  bad += 1
  puts "MISMATCH #{name} #{path}: expected #{val.inspect[0, 120]} got #{g.inspect[0, 120]}" if bad <= 15
end
err = File.read("#{out.path}.err")
puts err[0, 500] unless err.empty?
puts "#{files.size} docs, #{expect.size} checks, #{bad} mismatches"
exit(bad.zero? ? 0 : 1)
