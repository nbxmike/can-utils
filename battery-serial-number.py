import can
import time

def main():
    canbus = can.interface.Bus(bustype='socketcan',
                        channel='can0',
                        bitrate=500000)
    index = 0
    time_start = time.time() 
    prefix_found = False
    while (prefix_found == False) and (index < 100) and ( 10 > (time.time() - time_start)) :
        can_msg = canbus.recv(1)
        try:
            if (can_msg.arbitration_id >> 18) == 0x300:
                prefix_found = True
            else:
                index = index + 1
        except:
            index = index & 0xffff
    if prefix_found:
        print("Battery serial number programmer")
        serial_text = input("Enter serial number: ")
        serial_number = int(serial_text)
        serial_number = serial_number & 0xfffff
        id2 = (serial_number >>16 ) & 0x0f
        id1 = (serial_number >>8 ) & 0xff
        id0 = serial_number & 0xff
        msg = can.Message(arbitration_id=0x18000000, data=[1, id2, id1, id0, 0, 0, 0, 0], is_extended_id=True)
        canbus.send(msg)
        
        index = 0
        serial_found = False
        while (serial_found == False) and (index <1000):
            can_msg = canbus.recv(1)
            try:
                if (can_msg.arbitration_id >> 18) == 0x300:
                    index = index + 1
                    if (can_msg.arbitration_id & 0xfffff) == serial_number:
                        serial_found = True
                else:
                    index = index + 1
            except:
                index = index & 0xffff
        if serial_found:
            print("Battery serial number successfully programmed")
        else:
            print("Battery serial number failed programmed")
    else:
        print("No battery found")


if __name__ == "__main__":
    main()
