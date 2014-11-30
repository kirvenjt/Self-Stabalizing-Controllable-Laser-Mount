all: SSCLM

MPU = MPUfiles

HDRS = $(MPU)/helper_3dmath.h $(MPU)/I2Cdev.h $(MPU)/MPU6050_6Axis_MotionApps20.h $(MPU)/MPU6050.h
CMN_OBJS = $(MPU)/I2Cdev.o $(MPU)/MPU6050.o 
DMP_OBJS = SSCLM.o

# Set DMP FIFO rate to 20Hz to avoid overflows on 3d demo.  See comments in
# MPU6050_6Axis_MotionApps20.h for details.

$(CMN_OBJS) $(DMP_OBJS) $(RAW_OBJS) : $(HDRS)

SSCLM: $(CMN_OBJS) $(DMP_OBJS)
	$(CXX) -o $@ $^ -lm -lpthread -lwiringPi

clean:
	rm -f $(CMN_OBJS) $(DMP_OBJS) $(D3D_OBJS) $(RAW_OBJS)  SSCLM
