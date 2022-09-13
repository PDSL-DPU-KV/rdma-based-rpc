fs = import('fs')
if not fs.exists('../pb')
    run_command('mkdir', '../pb')
    message('Generating echo pb files...')
    run_command(protoc.full_path(), '--cpp_out=../pb', 'hello.proto')
endif