/* Actual configure-time runtime contract, not a new credential/keytab. The
 * native resolver identifies a backend; it does not read/write a principal. */
#include <krb5.h>
#include <stdio.h>
int main(void)
{
    krb5_context context=0;krb5_keytab keytab=0;
    krb5_error_code error=krb5_init_context(&context);
    if(error){fprintf(stderr,"krb5 context unavailable: %ld\n",(long)error);return 1;}
    error=krb5_kt_resolve(context,"WRFILE:api",&keytab);
    printf("WRFILE_supported=%d error=%ld\n",!error,(long)error);
    if(!error)krb5_kt_close(context,keytab);
    krb5_free_context(context);return error?2:0;
}
