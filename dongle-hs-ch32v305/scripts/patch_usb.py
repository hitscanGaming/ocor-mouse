import os

def patch_device():
    path = 'User/ch32v30x_usbhs_device.c'
    new_lines = []
    patched_def = False
    patched_dma = False
    
    with open(path, 'r', encoding='gbk', errors='ignore') as f:
        lines = f.readlines()
        
    for line in lines:
        new_lines.append(line)
        
        # Patch Definition
        if not patched_def:
            if 'USBHS_EP2_TX_Buf' in line and '__attribute__' in line and 'extern' not in line:
                # This is the definition line
                new_line = '__attribute__ ((aligned(4))) uint8_t USBHS_EP3_TX_Buf[ DEF_USB_EP3_HS_SIZE ];    //ep3_in(64)\n'
                new_lines.append(new_line)
                patched_def = True
                print("Patched definition")

        # Patch DMA
        if not patched_dma:
            if 'USBHS_EP2_TX_Buf' in line and 'UEP2_TX_DMA' in line:
                # Determine indentation
                indent = line[:line.find('USBHSD')]
                new_line = indent + 'USBHSD->UEP3_TX_DMA = (uint32_t)(uint8_t *)USBHS_EP3_TX_Buf;\n'
                new_lines.append(new_line)
                patched_dma = True
                print("Patched DMA")
                
    with open(path, 'w', encoding='gbk', errors='ignore') as f:
        f.writelines(new_lines)

if __name__ == '__main__':
    try:
        patch_device()
    except Exception as e:
        print(f"Error: {e}")
