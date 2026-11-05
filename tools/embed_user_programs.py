from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--rtti',action='store_true');parser.add_argument('--webkit-vm',action='store_true');parser.add_argument('--webkit-random',action='store_true');parser.add_argument('--webkit-sync',action='store_true');parser.add_argument('--feature-main',action='store_true');parser.add_argument('--libm',action='store_true');parser.add_argument('--jsc-support',action='store_true');parser.add_argument('--stdio',action='store_true');parser.add_argument('--jsc',action='store_true');parser.add_argument('--jsc-spin',action='store_true');parser.add_argument('--webcore-loop',action='store_true');parser.add_argument('--user-http',action='store_true');parser.add_argument('--resource-loop',action='store_true');parser.add_argument('--webcore-resource',action='store_true');parser.add_argument('--cairo-raster',action='store_true');parser.add_argument('--native-fonts',action='store_true');parser.add_argument('--font-paint',action='store_true');parser.add_argument('--font-select',action='store_true');parser.add_argument('--openssl',action='store_true');parser.add_argument('--native-page',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
out=[]
for name in ('vm','vm_guard','vm_decommit','vm_readonly','random','random_unready','context','context_cancel','platform','calendar'):
    data=(root/f'build/user-programs/{name}.elf').read_bytes()
    out.append(f'static const unsigned char user_{name}_elf[]={{'+','.join(str(v) for v in data)+'};')
for name in ('exit','loop','fault','sacx','memory','memory_fault','cpp','arena','library','library_fail','threads','threads_fault','threads_wait','threads_oom','threads_bss','threads_spin','lifetime','lifetime_recursive','lifetime_limits','lifetime_overflow','sync','sync_invalid','stdthreads','stdthreads_limit','stdthreads_unjoined','stdthreads_detach','keys'):
    data=(root/f'build/user-programs/{name}.elf').read_bytes()
    out.append(f'static const unsigned char user_{name}_elf[]={{'+','.join(str(v) for v in data)+'};')
assembly=[]
if args.native_page:
    import hashlib,json
    report=json.loads((root/'build/webcore-headless-page/report.json').read_text())
    if not report.get('linked') or not report.get('loader_accepted'):raise SystemExit('Native Page must link and pass the production loader first')
    for relative,key in [('tools/webcore_dihos/build_page_fixture.py','builder_sha256'),('build/webcore-engine-openssl/report.json','engine_report_sha256'),('build/webcore-headless-page/compile-report.json','client_compile_report_sha256')]:
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=report[key]:raise SystemExit('Native Page build input changed: '+relative)
    client=json.loads((root/'build/webcore-headless-page/compile-report.json').read_text())
    if hashlib.sha256((root/'sdk/sacx/include/sacx_user_window.h').read_bytes()).hexdigest()!=client['window_header_sha256']:raise SystemExit('Native window ABI changed')
    if report.get('window'):out.append('#define USER_NATIVE_PAGE_WINDOW 1')
    if report.get('navigation'):
        if not args.user_http:raise SystemExit('Native navigation requires the modeled HTTP backend (-UserHttp)')
        out.append('#define USER_NATIVE_PAGE_NAVIGATION 1')
        if report.get('https'):out.append('#define USER_NATIVE_PAGE_HTTPS 1')
        if report.get('embedded'):out.append('#define USER_NATIVE_PAGE_EMBEDDED 1')
        for relative,digest in client.get('navigation_inputs',{}).items():
            if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Native navigation changed: '+relative)
    for name,key in [('headless_page_fixture.cpp','fixture_sha256'),('headless_page_strategies.h','strategies_sha256'),('headless_page_platform.cpp','platform_sha256')]:
        if hashlib.sha256((root/'tools/webcore_dihos'/name).read_bytes()).hexdigest()!=client[key]:raise SystemExit('Native Page client changed: '+name)
    path='build/webcore-headless-page/page.elf';data=(root/path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['elf_sha256']:raise SystemExit('Native Page executable changed')
    out.append('#define USER_HAS_NATIVE_PAGE 1')
    out.append(f'extern const unsigned char user_native_page_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_native_page,"a"\n.balign 16\n.global user_native_page_elf\nuser_native_page_elf:\n.incbin "{path}"\n')
if args.openssl:
    import hashlib,json
    for directory,symbol,profile in [('user-openssl','user_openssl','known-answers'),('user-openssl-unready','user_openssl_unready','unready')]:
        report=json.loads((root/f'build/{directory}/report.json').read_text())
        if report.get('profile')!=profile:raise SystemExit('Wrong crypto test profile')
        if not report.get('linked') or not report.get('loader_accepted'):raise SystemExit('Native crypto fixture not loader validated')
        for relative,key in [('tools/webcore_dihos/build_crypto_fixture.py','builder_sha256'),('tools/webcore_dihos/openssl/crypto_fixture.cpp','fixture_sha256'),('build/webcore-openssl/report.json','crypto_report_sha256')]:
            if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=report[key]:raise SystemExit('Native crypto input changed: '+relative)
        native=json.loads((root/'build/webcore-openssl/report.json').read_text())
        if native['builder_sha256']!=hashlib.sha256((root/'tools/webcore_dihos/build_openssl.py').read_bytes()).hexdigest():raise SystemExit('Native crypto build recipe changed')
        for relative,digest in native['inputs'].items():
            if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Native crypto adapter changed: '+relative)
        path=f'build/{directory}/crypto.elf';data=(root/path).read_bytes()
        if hashlib.sha256(data).hexdigest()!=report['elf_sha256']:raise SystemExit('Native crypto ELF changed')
        out.append('#define USER_HAS_OPENSSL 1') if profile=='known-answers' else None
        out.append(f'extern const unsigned char {symbol}_elf[{len(data)}];')
        assembly.append(f'.section .rodata.{symbol},"a"\n.balign 16\n.global {symbol}_elf\n{symbol}_elf:\n.incbin "{path}"\n')
if args.native_fonts or args.font_paint or args.font_select:
    import hashlib,json
    directory='user-font-select' if args.font_select else 'user-font-paint' if args.font_paint else 'user-fonts'
    report=json.loads((root/f'build/{directory}/report.json').read_text())
    if report.get('fixture_profile')!=('selection' if args.font_select else 'paint' if args.font_paint else 'shaping'):raise SystemExit('Wrong font fixture profile')
    if args.font_select and hashlib.sha256((root/'build/webcore-fontconfig/report.json').read_bytes()).hexdigest()!=report['fontconfig_report_sha256']:raise SystemExit('Changed font selection library')
    if args.font_select and hashlib.sha256((root/'build/webcore-fontconfig-graphics/report.json').read_bytes()).hexdigest()!=report['fontconfig_graphics_report_sha256']:raise SystemExit('Changed font selection painting backend')
    if (args.font_paint or args.font_select) and hashlib.sha256((root/'build/webcore-font-graphics/report.json').read_bytes()).hexdigest()!=report['font_graphics_report_sha256']:raise SystemExit('Changed font painting libraries')
    if not report.get('linked') or not report.get('loader_accepted'):raise SystemExit('Native font fixture not validated')
    for relative,digest in report['inputs'].items():
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Font input changed: '+relative)
    for relative,key in [('tools/webcore_dihos/build_font_fixture.py','builder_sha256'),('build/webcore-fonts/report.json','font_report_sha256')]:
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=report[key]:raise SystemExit('Font build proof changed: '+relative)
    path=f'build/{directory}/fonts.elf';data=(root/path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['elf_sha256']:raise SystemExit('Font ELF changed')
    out.append('#define USER_HAS_NATIVE_FONTS 1')
    if args.font_select:out.append('#define USER_FONTS_SELECTION 1')
    if args.font_paint or args.font_select:out.append('#define USER_FONTS_PAINT 1')
    out.append(f'extern const unsigned char user_fonts_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_fonts,"a"\n.balign 16\n.global user_fonts_elf\nuser_fonts_elf:\n.incbin "{path}"\n')
if args.cairo_raster:
    import hashlib,json
    for directory,symbol,profile in [('user-cairo','cairo_raster','raster'),('user-cairo-oom','cairo_oom','critical-oom')]:
        report=json.loads((root/f'build/{directory}/report.json').read_text())
        if not report.get('linked') or not report.get('loader_accepted') or report.get('fixture_profile')!=profile:raise SystemExit('Native Cairo ELF not validated')
        inputs=[('tools/webcore_dihos/graphics/raster_fixture.cpp',report['fixture_sha256']),
                ('tools/webcore_dihos/build_raster_fixture.py',report['builder_sha256']),
                ('build/webcore-graphics/report.json',report['graphics_report_sha256']),
                *report['native_sources'].items()]
        for relative,digest in inputs:
            if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Cairo input changed: '+relative)
        path=f'build/{directory}/raster.elf';data=(root/path).read_bytes()
        if hashlib.sha256(data).hexdigest()!=report['elf_sha256']:raise SystemExit('Cairo ELF changed')
        out.append('#define USER_HAS_CAIRO_RASTER 1')
        out.append(f'extern const unsigned char user_{symbol}_elf[{len(data)}];')
        assembly.append(f'.section .rodata.user_{symbol},"a"\n.balign 16\n.global user_{symbol}_elf\nuser_{symbol}_elf:\n.incbin "{path}"\n')
if args.webcore_resource:
    import hashlib,json
    report=json.loads((root/'build/webcore-resource-bridge/report.json').read_text())
    if not report.get('linked') or not report.get('loader_accepted'):raise SystemExit('Native WebCore resource ELF not validated')
    path='build/webcore-resource-bridge/webcore-resource.elf';data=(root/path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['elf_sha256']:raise SystemExit('WebCore resource ELF changed')
    if hashlib.sha256((root/'tools/webcore_dihos/webcore_resource_fixture.cpp').read_bytes()).hexdigest()!=report['fixture_sha256']:raise SystemExit('WebCore resource fixture changed')
    if hashlib.sha256((root/'tools/webcore_dihos/build_resource_bridge.py').read_bytes()).hexdigest()!=report['builder_sha256']:raise SystemExit('WebCore resource builder changed')
    if hashlib.sha256((root/'build/user-jsc-native/resource-report.json').read_bytes()).hexdigest()!=report['base_report_sha256']:raise SystemExit('WebCore resource base dependencies changed')
    for relative,digest in report['inputs'].items():
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('WebCore resource input changed: '+relative)
    out.append('#define USER_HAS_WEBCORE_RESOURCE 1')
    out.append(f'extern const unsigned char user_webcore_resource_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_webcore_resource,"a"\n.balign 16\n.global user_webcore_resource_elf\nuser_webcore_resource_elf:\n.incbin "{path}"\n')
for name in ('sparse','sparse_guard','sparse_decommit','sparse_readonly','sparse_spin','sparse_nx'):
    data=(root/f'build/user-programs/{name}.elf').read_bytes()
    out.append(f'static const unsigned char user_{name}_elf[]={{'+','.join(str(v) for v in data)+'};')
if args.jsc:
    import hashlib,json,re
    report=json.loads((root/'build/user-jsc-native/report.json').read_text())
    if not report.get('linked') or not report.get('loader_accepted'):raise SystemExit('Native JSC executable must link and pass the production loader validator first')
    key=Path(report['output']).name
    if not re.fullmatch('[0-9a-f]{12}',key):raise SystemExit('Unexpected native JSC artifact directory')
    path=f'build/user-jsc-native/{key}/jsc.elf';data=(root/path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['elf_sha256'] or hashlib.sha256((root/'sdk/sacx/apps/jsc_probe/main.cpp').read_bytes()).hexdigest()!=report['fixture_sha256']:
        raise SystemExit('Native JSC executable or fixture source changed')
    out.append('#define USER_HAS_JSC 1')
    out.append(f'extern const unsigned char user_jsc_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_jsc,"a"\n.balign 16\n.global user_jsc_elf\nuser_jsc_elf:\n.incbin "{path}"\n')
if args.jsc_spin:
    import hashlib,json,re
    if not args.jsc:raise SystemExit('--jsc-spin requires --jsc')
    report=json.loads((root/'build/user-jsc-native/spin-report.json').read_text())
    if not report.get('linked') or not report.get('loader_accepted') or report.get('fixture_profile')!='spin':
        raise SystemExit('The infinite-JavaScript fixture must link and pass loader validation')
    key=Path(report['output']).name
    if not re.fullmatch('[0-9a-f]{12}',key):raise SystemExit('Unexpected native JSC spin artifact directory')
    path=f'build/user-jsc-native/{key}/jsc.elf';data=(root/path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['elf_sha256'] or hashlib.sha256((root/'sdk/sacx/apps/jsc_probe/main.cpp').read_bytes()).hexdigest()!=report['fixture_sha256']:
        raise SystemExit('Native JSC spin executable or source changed')
    out.append('#define USER_HAS_JSC_SPIN 1')
    out.append(f'extern const unsigned char user_jsc_spin_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_jsc_spin,"a"\n.balign 16\n.global user_jsc_spin_elf\nuser_jsc_spin_elf:\n.incbin "{path}"\n')
if args.stdio:
    import hashlib,json
    path='build/user-stdio/stdio.elf';data=(root/path).read_bytes()
    report=json.loads((root/'build/user-stdio/report.json').read_text());proof=report['fixture']
    if hashlib.sha256(data).hexdigest()!=proof['elf_sha256']:raise SystemExit('Native stdio fixture ELF changed')
    for relative,digest in [('tools/user_stdio_program.cpp',proof['source_sha256']),('tools/jsc_dihos/StdioDihOS.cpp',report['backend_sha256'])]:
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Native stdio fixture source changed')
    out.append('#define USER_HAS_STDIO 1')
    out.append(f'extern const unsigned char user_stdio_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_stdio,"a"\n.balign 16\n.global user_stdio_elf\nuser_stdio_elf:\n.incbin "{path}"\n')
if args.jsc_support:
    import hashlib,json
    path='build/user-jsc-support/support.elf';data=(root/path).read_bytes()
    proof=json.loads((root/'build/user-jsc-support/report.json').read_text())['fixture']
    for relative,key in [('tools/user_jsc_support_program.cpp','source_sha256'),('tools/user_arithmetic_vectors.inc','vectors_sha256')]:
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=proof[key]:raise SystemExit('JSC support fixture source changed')
    if hashlib.sha256(data).hexdigest()!=proof['elf_sha256']:raise SystemExit('JSC support fixture ELF changed')
    out.append('#define USER_HAS_JSC_SUPPORT 1')
    out.append(f'extern const unsigned char user_jsc_support_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_jsc_support,"a"\n.balign 16\n.global user_jsc_support_elf\nuser_jsc_support_elf:\n.incbin "{path}"\n')
if args.libm:
    import hashlib,json
    path='build/user-libm/math.elf';data=(root/path).read_bytes()
    math_report=json.loads((root/'build/user-libm/report.json').read_text())
    proof=math_report['fixture']
    if hashlib.sha256(data).hexdigest()!=proof['elf_sha256'] or hashlib.sha256((root/'tools/user_math_program.cpp').read_bytes()).hexdigest()!=proof['source_sha256']:
        raise SystemExit('Native math fixture changed')
    out.append('#define USER_HAS_LIBM 1')
    out.append('#define USER_LIBM_C_SUPPORT '+str(int(math_report.get('c_support',False))))
    out.append(f'extern const unsigned char user_math_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_math,"a"\n.balign 16\n.global user_math_elf\nuser_math_elf:\n.incbin "{path}"\n')
if args.feature_main:
    import hashlib,json
    proof=json.loads((root/'build/jsc-dihos/report.json').read_text()).get('feature_main_bridge',{})
    if not proof.get('required_dependency_rejected'):raise SystemExit('Native main dependency rejection was not verified')
    out.append('#define USER_HAS_FEATURE_MAIN 1')
    for name in ('c','cpp0','cpp2'):
        path=f'build/jsc-dihos/feature-main-{name}.elf';size=(root/path).stat().st_size
        if hashlib.sha256((root/path).read_bytes()).hexdigest()!=proof[name]['sha256']:
            raise SystemExit('Native main bridge fixture changed: '+name)
        out.append(f'extern const unsigned char user_feature_{name}_elf[{size}];')
        assembly.append(f'.section .rodata.user_feature_{name},"a"\n.balign 16\n.global user_feature_{name}_elf\nuser_feature_{name}_elf:\n.incbin "{path}"\n')
if args.webkit_sync:
    out.append('#define USER_HAS_WEBKIT_SYNC 1')
    for name in ('sync','invalid','invalid_key'):
        path=f'build/user-webkit-sync/{name}.elf';size=(root/path).stat().st_size
        out.append(f'extern const unsigned char user_webkit_{name}_elf[{size}];')
        assembly.append(f'.section .rodata.user_webkit_{name},"a"\n.balign 16\n.global user_webkit_{name}_elf\nuser_webkit_{name}_elf:\n.incbin "{path}"\n')
if args.webkit_random:
    out.append('#define USER_HAS_WEBKIT_RANDOM 1')
    for name in ('bmalloc','wtf'):
        path=f'build/user-webkit-random/random-{name}.elf';size=(root/path).stat().st_size
        out.append(f'extern const unsigned char user_random_{name}_elf[{size}];')
        assembly.append(f'.section .rodata.user_random_{name},"a"\n.balign 16\n.global user_random_{name}_elf\nuser_random_{name}_elf:\n.incbin "{path}"\n')
if args.webkit_vm:
    path='build/user-webkit-vm/webkit-vm.elf';size=(root/path).stat().st_size
    out.append('#define USER_HAS_WEBKIT_VM 1')
    out.append(f'extern const unsigned char user_webkit_vm_elf[{size}];')
    assembly.append(f'.section .rodata.user_webkit_vm,"a"\n.balign 16\n.global user_webkit_vm_elf\nuser_webkit_vm_elf:\n.incbin "{path}"\n')
if args.rtti:
    path='build/user-rtti/rtti.elf';size=(root/path).stat().st_size
    out.append('#define USER_HAS_RTTI 1')
    out.append(f'extern const unsigned char user_rtti_elf[{size}];')
    assembly.append(f'.section .rodata.user_rtti,"a"\n.balign 16\n.global user_rtti_elf\nuser_rtti_elf:\n.incbin "{path}"\n')
if args.webcore_loop:
    import hashlib,json,re
    report=json.loads((root/'build/user-jsc-native/eventloop-report.json').read_text())
    if not report['linked'] or not report['loader_accepted']:raise SystemExit('Native event-loop ELF was not validated')
    key=Path(report['output']).name
    if not re.fullmatch('[0-9a-f]{12}',key):raise SystemExit('Unexpected native event-loop artifact path')
    path=f'build/user-jsc-native/{key}/jsc.elf';data=(root/path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['elf_sha256']:raise SystemExit('Native event-loop ELF changed')
    sources=dict(report['event_loop_sources']);sources['tools/webcore_dihos/eventloop_fixture.cpp']=report['fixture_sha256']
    sources['tools/build_jsc_native_test.py']=report['builder_sha256']
    for relative,digest in sources.items():
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Native event-loop source changed: '+relative)
    out.append('#define USER_HAS_WEBCORE_LOOP 1')
    out.append(f'extern const unsigned char user_webcore_loop_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_webcore_loop,"a"\n.balign 16\n.global user_webcore_loop_elf\nuser_webcore_loop_elf:\n.incbin "{path}"\n')
if args.resource_loop:
    import hashlib,json,re
    report=json.loads((root/'build/user-jsc-native/resource-report.json').read_text())
    if not report['linked'] or not report['loader_accepted']:raise SystemExit('Native resource ELF was not validated')
    key=Path(report['output']).name
    if not re.fullmatch('[0-9a-f]{12}',key):raise SystemExit('Unexpected native resource artifact path')
    path=f'build/user-jsc-native/{key}/jsc.elf';data=(root/path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['elf_sha256']:raise SystemExit('Native resource ELF changed')
    sources=dict(report['event_loop_sources']);sources['tools/webcore_dihos/resource_fixture.cpp']=report['fixture_sha256']
    sources['tools/build_jsc_native_test.py']=report['builder_sha256']
    for relative,digest in sources.items():
        if hashlib.sha256((root/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Native resource source changed: '+relative)
    out.append('#define USER_HAS_RESOURCE_LOOP 1')
    out.append(f'extern const unsigned char user_resource_loop_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_resource_loop,"a"\n.balign 16\n.global user_resource_loop_elf\nuser_resource_loop_elf:\n.incbin "{path}"\n')
if args.user_http:
    import hashlib
    path='build/user-http/http.elf';data=(root/path).read_bytes()
    out.append('#define USER_HAS_HTTP 1')
    out.append(f'extern const unsigned char user_http_elf[{len(data)}];')
    assembly.append(f'.section .rodata.user_http,"a"\n.balign 16\n.global user_http_elf\nuser_http_elf:\n.incbin "{path}"\n')
for name in ('large','large_fault'):
    path=f'build/user-programs/{name}.elf';size=(root/path).stat().st_size
    out.append(f'extern const unsigned char user_{name}_elf[{size}];')
    assembly.append(f'.section .rodata.user_{name},"a"\n.balign 16\n.global user_{name}_elf\nuser_{name}_elf:\n.incbin "{path}"\n')
(root/'build/user-runtime/user_program_blobs.S').write_text('\n'.join(assembly))
(root/'build/user-runtime/user_program.inc').write_text('\n'.join(out))
