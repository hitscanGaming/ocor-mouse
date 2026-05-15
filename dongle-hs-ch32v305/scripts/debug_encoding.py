path = 'User/ch32v30x_usbhs_device.c'
with open(path, 'r', encoding='gbk', errors='ignore') as f: 
    for line in f:
        if 'USBHS_EP2_TX_Buf' in line:
            print(repr(line))
