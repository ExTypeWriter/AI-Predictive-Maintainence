import serial
import keyboard
import os
# การตั้งค่าพอร์ตและความเร็ว
PORT = 'COM11' 
BAUDRATE = 115200
FILENAME = 'fail_data_speed_1.csv'

data_list = []
MAX_ROWS = 125000

print(f"Connecting to {PORT} at {BAUDRATE} baud...")

try:
    # เปิดการเชื่อมต่อ Serial Port
    ser = serial.Serial(PORT, BAUDRATE, timeout=1)
    print("Connected! Reading data...")
    print(">>> Press 'q' to stop and save the data. <<<")

    while True:
        # # ตรวจจับการกดปุ่ม 'q' เพื่อออกจากลูป
        # if keyboard.is_pressed('q'):
        #     print("\n'q' pressed. Stopping data collection...")
        #     break

        # ตรวจสอบว่ามีข้อมูลส่งเข้ามาหรือไม่
        if ser.in_waiting > 0:
            # อ่านข้อมูล 1 บรรทัด ลบช่องว่าง และแปลงเป็นสตริง
            line = ser.readline().decode('utf-8').strip()
            if line: 
                data_list.append(line)
                print(line) # แสดงผลข้อมูลที่อ่านได้บนหน้าจอ
                if len(data_list) >= MAX_ROWS:
                    print(f"\nReached {MAX_ROWS} rows. Stopping data collection...")
                    break

except serial.SerialException as e:
    print(f"\nError opening serial port: {e}")
except Exception as e:
    print(f"\nAn error occurred: {e}")
finally:
    # ปิดการเชื่อมต่อพอร์ตเสมอเมื่อเสร็จสิ้น
    if 'ser' in locals() and ser.is_open:
        ser.close()

    # บันทึกข้อมูลลงไฟล์ CSV
    if data_list:
        # ดึง Path ของโฟลเดอร์ที่สคริปต์นี้ทำงานอยู่
        script_dir = os.path.dirname(os.path.abspath(__file__))
        file_path = os.path.join(script_dir, FILENAME)

        print(f"\nSaving {len(data_list)} records to {file_path}...")
        
        # เขียนข้อมูลลงไฟล์
        with open(file_path, 'w', encoding='utf-8') as f:
            for item in data_list:
                f.write(f"{item}\n")
        
        print("Save complete!")
    else:
        print("\nNo data was collected.")