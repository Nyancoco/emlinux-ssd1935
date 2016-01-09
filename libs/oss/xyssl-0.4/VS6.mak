
# SSE2 support is disabled -- add /DHAVE_SSE2 to enable it

CFLAGS=/O2 /W3 /MT /nologo /Fo$@ /DWIN32 /DSELF_TEST

LIB_OBJ=src/aes.obj        src/arc4.obj       src/base64.obj     \
        src/bignum.obj     src/dhm.obj        src/des.obj        \
        src/havege.obj     src/md2.obj        src/md4.obj        \
        src/md5.obj        src/net.obj        src/rsa.obj        \
        src/sha1.obj       src/sha2.obj       src/ssl_v3.obj     \
        src/ssl_cli.obj    src/ssl_srv.obj    src/testcert.obj   \
        src/timing.obj     src/x509_in.obj

APP_OBJ=app/benchmark.exe  app/hello.exe      app/filecrypt.exe  \
        app/rsa_demo.exe   app/selftest.exe   app/ssl_client.exe \
        app/ssl_server.exe

LIBS=xyssl.lib kernel32.lib shell32.lib user32.lib

default: xyssl.lib apps

xyssl.lib: $(LIB_OBJ) ; @echo.
	@lib /out:xyssl.lib $(LIB_OBJ)
	@del src\*.obj

apps: $(APP_OBJ) ; @echo.
	@del app\*.exe

.c.obj: ; @$(CC) $(CFLAGS) /c $<

.c.exe: ; @$(CC) $(CFLAGS) /Isrc $(LIBS) $<

