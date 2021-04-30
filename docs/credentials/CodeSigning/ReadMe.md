# Code Signing Certificate

These files are for the code signing certificate.  The most useful in here is
that of the private key which is utilized to generate out new certificates when
necessary (every 365 days).  This is necessary in order for new firmware images
to be able to be signed.

To generate out a new certificate a single command is needed:

`openssl req -new -x509 -config cert_config.txt -extensions my_exts -nodes -days 365 -key ecdsasigner.key -out ecdsasigner.crt`

Once this certificate has been generated, you need to go to the AWS console and
navigate to the Certificate Manager.  Inside of the Certificate Manager find the
certificate labeled: "Firmware Code Signer".  To update the certificate, press
the button: "Reimport Certificate" and copy the certificate and private key and
paste them into their respective form fields.

You should now be able to proceed with life as normal.
