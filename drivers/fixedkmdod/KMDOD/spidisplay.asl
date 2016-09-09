//
// To compile this file to ACPITABL.dat, run makeacpi.cmd from a razzle prompt.
// Copy ACPITABL.dat to %windir%\system32, turn on testsigning, and reboot.
//

DefinitionBlock ("ACPITABL.dat", "SSDT", 1, "MSFT", "DISPLAY", 1)
{

    Scope (\_SB)
    {
        Device(DSP0)
        {
            Name(_HID, "MSFT8002")
            Name(_CID, "MSFT8002")
            Name(_UID, 1)

            Name(_CRS, ResourceTemplate()
            {
				// low-speed control channel (1Mhz)
                SPISerialBus(
					0,
                    PolarityLow,
                    FourWireMode,
                    8,
                    ControllerInitiated,
                    1000000,
                    ClockPolarityLow,
                    ClockPhaseFirst,
                    "\\_SB.SPI0",
                    0,
                    )
				
				// high-speed data channel (48Mhz)
				SPISerialBus(
					0,
                    PolarityLow,
                    FourWireMode,
                    8,
                    ControllerInitiated,
                    48000000,
                    ClockPolarityLow,
                    ClockPhaseFirst,
                    "\\_SB.SPI0",
                    0,
                    )

				
				// GPIO25 - TFT_DC_3V
                GpioIO(Shared, PullUp, 0, 0, IoRestrictionNone, "\\_SB.GPI0", 0, ResourceConsumer, , ) { 25 }
            })
        }
    }
}