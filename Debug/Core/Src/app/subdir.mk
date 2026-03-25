################################################################################
# 自动生成的文件。不要编辑！
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app/Receive.c \
../Core/Src/app/app_printer.c \
../Core/Src/app/dotmatrix_converter_debug.c \
../Core/Src/app/escpos_commands.c \
../Core/Src/app/print_buffer.c \
../Core/Src/app/print_stream_parser.c \
../Core/Src/app/printer_output.c \
../Core/Src/app/receipt_job_buffer.c 

S_UPPER_SRCS += \
../Core/Src/app/ku_font.S 

OBJS += \
./Core/Src/app/Receive.o \
./Core/Src/app/app_printer.o \
./Core/Src/app/dotmatrix_converter_debug.o \
./Core/Src/app/escpos_commands.o \
./Core/Src/app/ku_font.o \
./Core/Src/app/print_buffer.o \
./Core/Src/app/print_stream_parser.o \
./Core/Src/app/printer_output.o \
./Core/Src/app/receipt_job_buffer.o 

S_UPPER_DEPS += \
./Core/Src/app/ku_font.d 

C_DEPS += \
./Core/Src/app/Receive.d \
./Core/Src/app/app_printer.d \
./Core/Src/app/dotmatrix_converter_debug.d \
./Core/Src/app/escpos_commands.d \
./Core/Src/app/print_buffer.d \
./Core/Src/app/print_stream_parser.d \
./Core/Src/app/printer_output.d \
./Core/Src/app/receipt_job_buffer.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/app/%.o Core/Src/app/%.su Core/Src/app/%.cyclo: ../Core/Src/app/%.c Core/Src/app/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32U575xx -c -I../Core/Inc -I../Core/Inc/app -I../Core/Inc/log -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include/ -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/ -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/ -I../Middlewares/Third_Party/CMSIS/RTOS2/Include/ -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Core/Src/app/%.o: ../Core/Src/app/%.S Core/Src/app/subdir.mk
	arm-none-eabi-gcc -mcpu=cortex-m33 -g3 -DDEBUG -c -I../Core/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc -I../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32U5xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include/ -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM33_NTZ/non_secure/ -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/ -I../Middlewares/Third_Party/CMSIS/RTOS2/Include/ -x assembler-with-cpp -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@" "$<"

clean: clean-Core-2f-Src-2f-app

clean-Core-2f-Src-2f-app:
	-$(RM) ./Core/Src/app/Receive.cyclo ./Core/Src/app/Receive.d ./Core/Src/app/Receive.o ./Core/Src/app/Receive.su ./Core/Src/app/app_printer.cyclo ./Core/Src/app/app_printer.d ./Core/Src/app/app_printer.o ./Core/Src/app/app_printer.su ./Core/Src/app/dotmatrix_converter_debug.cyclo ./Core/Src/app/dotmatrix_converter_debug.d ./Core/Src/app/dotmatrix_converter_debug.o ./Core/Src/app/dotmatrix_converter_debug.su ./Core/Src/app/escpos_commands.cyclo ./Core/Src/app/escpos_commands.d ./Core/Src/app/escpos_commands.o ./Core/Src/app/escpos_commands.su ./Core/Src/app/ku_font.d ./Core/Src/app/ku_font.o ./Core/Src/app/print_buffer.cyclo ./Core/Src/app/print_buffer.d ./Core/Src/app/print_buffer.o ./Core/Src/app/print_buffer.su ./Core/Src/app/print_stream_parser.cyclo ./Core/Src/app/print_stream_parser.d ./Core/Src/app/print_stream_parser.o ./Core/Src/app/print_stream_parser.su ./Core/Src/app/printer_output.cyclo ./Core/Src/app/printer_output.d ./Core/Src/app/printer_output.o ./Core/Src/app/printer_output.su ./Core/Src/app/receipt_job_buffer.cyclo ./Core/Src/app/receipt_job_buffer.d ./Core/Src/app/receipt_job_buffer.o ./Core/Src/app/receipt_job_buffer.su

.PHONY: clean-Core-2f-Src-2f-app

