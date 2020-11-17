import can
import time

def main():
    canbus = can.interface.Bus(bustype='socketcan',
                        channel='can0',
                        bitrate=500000)
    # Wait to see a battery message - any battery reporting 0x300
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
        except:   # This is really a do nothing to make try work
            index = index & 0xffff
    if prefix_found:
        print("Battery serial number programmer")
        # get the desired serial number for the user and build a programming message
        serial_text = input("Enter serial number: ")
        serial_number = int(serial_text)
        serial_number = serial_number & 0xfffff
        id2 = (serial_number >>16 ) & 0x0f
        id1 = (serial_number >>8 ) & 0xff
        id0 = serial_number & 0xff
        msg = can.Message(arbitration_id=0x18000000, data=[1, id2, id1, id0, 0, 0, 0, 0], is_extended_id=True)
        canbus.send(msg)
        # Now look for a battery reporting with the new serial number
        index = 0
        serial_found = False
        time_start = time.time() 
        while (serial_found == False) and (index <1000) and ( 10 > (time.time() - time_start)) :
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
            print("Battery serial number programmed")
            # Clear out any critical error that pre-existed, first send a 600 with CRIT_RESET
            msg = can.Message(arbitration_id=0x18000000, data=[2, id2, id1, id0, 0, 0, 0, 0], is_extended_id=True)
            # look for a 300 with status 0x80 
            index = 0
            status_ok = False
            time_start = time.time() 
            while (status_ok == False) and (index <1000) and ( 10 > (time.time() - time_start)):
                can_msg = canbus.recv(1)
                try:
                    if (can_msg.arbitration_id >> 18) == 0x300:
                        index = index + 1
                        if (can_msg.arbitration_id & 0xfffff) == serial_number:
                            if  can_msg.data[7] == 0x80 :
                                status_ok = True
                    else:
                        index = index + 1
                except:
                    index = index & 0xffff
            if status_ok:
                print("Status clear command 0x600 accepted")
                # Finally reset the battery to ensure that the error code byte is cleared
                msg = can.Message(arbitration_id=0x10000000, data=[4, id2, id1, id0, 0, 0, 0, 0], is_extended_id=True)
                # look for a 300 with status 0x80 and error code of zero, but since the BMS cycles through error code need to check for a while 
                index = 0
                redidual_error_ok = False
                time_start = time.time() 
                while (index <40) and ( 10 > (time.time() - time_start)):
                    can_msg = canbus.recv(1)
                    try:
                        if (can_msg.arbitration_id >> 18) == 0x300:
                            index = index + 1
                            if (can_msg.arbitration_id & 0xfffff) == serial_number:
                                if  (can_msg.data[7] == 0x80) and (can_msg.data[6] == 0x00) :
                                    redidual_error_ok = True
                                else:
                                    redidual_error_ok = False
                    except:
                        index = index & 0xffff
                if redidual_error_ok:
                    print("Battery Programmed and Cleared of errors successfully")
                else:
                    print ("Battery programmed but reporting errors")
            else:
                print("Battery clear status command 0x600 failed")
            
        else:
            print("Battery serial number failed programmed")
    else:
        print("No battery found")


if __name__ == "__main__":
    main()
