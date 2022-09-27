fs = import('fs')
if not fs.exists('../pb')
    run_command('mkdir', '../pb', check: true)
endif
message('Generating echo pb files...')

run_command(protoc, '--cpp_out=../pb', 'bench.proto', check: true)
