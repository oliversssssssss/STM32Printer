################################################################################
# 自动生成的文件。不要编辑！
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/log/myprintf.c 

OBJS += \
./Core/Src/log/myprintf.o 

C_DEPS += \
./Core/Src/log/myprintf.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/log/%.o Core/Src/log/%.su Core/Src/log/%.cyclo: ../Core/Src/log/%.c Core/Src/log/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32U575xx -c -I../Core/Inc -I../Core/Inc/app -I../Core/Inc/log -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include/ -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/ -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/ -I../Middlewares/Third_Party/CMSIS/RTOS2/Include/ -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-log

clean-Core-2f-Src-2f-log:
	-$(RM) ./Core/Src/log/myprintf.cyclo ./Core/Src/log/myprintf.d ./Core/Src/log/myprintf.o ./Core/Src/log/myprintf.su

.PHONY: clean-Core-2f-Src-2f-log

