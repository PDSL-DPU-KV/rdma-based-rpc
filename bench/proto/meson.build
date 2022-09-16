fs = import('fs')
if not fs.exists('../pb')
    run_command('mkdir', '../pb', check: true)
endif
message('Generating echo pb files...')

run_command(
    find_program(get_option('protoc_path'), version: '= 3.20.1'),
    '--cpp_out=../pb',
    'bench.proto',
    check: true,
)
