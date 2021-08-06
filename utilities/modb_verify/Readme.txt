modb_verify.py

Utility for verifying encryption is enabled and implemented for the ESP32 on the Model-B Appliance.

Typical output on an encrypted system:

C:\Users\ian.whitehead\GitHub\dw_ModelB\modules\utilities\modb_verify>modb_verify.py -h
usage: modb_verify.py [-h] port

Drinkworks Model-B ESP32 Firmware Verifier, v1.0

positional arguments:
  port        set ESP Programming Port, e.g. COM17

optional arguments:
  -h, --help  show this help message and exit
  
C:\Users\ian.whitehead\GitHub\dw_ModelB\modules\utilities\modb_verify>modb_verify.py COM17
Deleting temporary image files
  Deleting: Bootloader.out
  Deleting: espFactory.out
  Deleting: PartitionTable.out
  Deleting: picFactory.out
  Deleting: picOTA.out
  Deleting: storage.out
Checking espfuse to see if keys are secure
  Flash encryption key: SECURE
  Secure boot key: SECURE

Checking: Bootloader, address: 00001000, length: 0000D000
  Found 6 strings of at least 8 bytes:
    N/[(UO(t
    JYk@HXg*
    QQPJB2f^
    NFz_FG72
    Ce<X!QE[
    #V eTJDW

Checking: PartitionTable, address: 0000E000, length: 00001000
  Found 0 strings of at least 8 bytes:

Checking: storage, address: 0001A000, length: 00010000
  Found 11 strings of at least 8 bytes:
    ;=E:3GZ}
    1[*PjCe?w
    1[*PjCe?w
    Z3T_Si9~<
    SZ3T_Si9~<
    6Y_`$-Ub
    -FxzD(<W|
    H:-FxzD(<W|
    c*Fb{&U2
    leX3AX2f:)B{
    leX3AX2f:)B{

Checking: picFactory, address: 00070000, length: 00030000
  Found 3 strings of at least 10 bytes:
    IYawGvFf1G9
    k@f.q}B'T(
    I>z4d34}VD

Checking: picOTA, address: 000A0000, length: 00030000
  Found 8 strings of at least 10 bytes:
    <)#|~oBcM~
    <)#|~oBcM~
    -NhxDZ*od%H
    2O9tL[F[~(
    2O9tL[F[~(
    P1#:Yw?FJ_
    **59|@OO}}
    vulhxA:f.Mx

Checking: espFactory, address: 00200000, length: 00100000
  Found 33 strings of at least 10 bytes:
    \@eLhkNE8/
    <SR8j<yM96
    ssh[hnJ#%2@>K
    \\v4a;d)AG
    AI:7qf3-{n
    A p+qmNAh.\6
    n({G}3/kMa
    jBi]){4D";
    /}.VMa,^;)2@
    gB.U>h2z_X
    C(_"2X<w,Xu
    $ot/wj<QXIF
    Tj@`QiZIeS
    _JO~?FU-I]qy~
    P%4ZL6?uR[
    CKm3-xu)]#
    xqgPgzE}jf
    Gh]gr;`g;Ezu?*
    Xb|*7fMlox
    xdhd!u}+*u
    ^rNX)n#N3}
    rp~(%DTE%{[
    ($@j91_U)d
    #R kEi"t}/"
    |_l*AL^LUu
    _F[g.Q+%>6
    5]t#G!n:KY-
    R%+=:p0RB!
    RaU;F7KjR^!
    @==9`#*!_1
    qcif&1BPcG'
    1}d4W>4M'
    6~5P!,\7{%
Exiting Program

C:\Users\ian.whitehead\GitHub\dw_ModelB\modules\utilities\modb_verify>