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
            if (can_msg.arbitration_id >> 18) == x400:
                prefix_found = True
            else:
                index = index + 1
        except:
            index = index & 0xffff
    if prefix_found:
        print("Battery serial number programmer")
        serial_text = input("Enter serial number: ")
        serial_number = int(serial_text)
        serial_number = serial_number &xfffff
        id2 = (serial_number >>16 ) & x0f
        id1 = (serial_number >>8 ) & xff
        id0 = serial_number & xff
        msg = can.Message(arbitration_id=0x14000000, data=[x80, 0, x85, 0, id2, id1, id0, 0], is_extended_id=True)
        bus.send(msg)
        
        index = 0
        serial_found = False
        while (serial_found == False) and (index <100):
            can_msg = canbus.recv(1)
            try:
                if (can_msg.arbitration_id >> 18) == x400:
                    index = index + 1
                    if (can_msg.arbitration_id & xfffff) == serial_number:
                        prefix_found = True
                else:
                    index = index + 1
            except:
                index = index & 0xffff
        if serial_found:
            print("Battery serial number successfully programmed")
    else:
        print("No battery found")


if __name__ == "__main__":
    main()
