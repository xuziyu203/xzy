# PLL Constraints
#################
# PLL Constraints
#################

create_clock -period 2.500 -name tx_cal_clk_90edge [get_ports {tx_cal_clk_90edge}]
create_clock -period 2.500 -name sdram_clk [get_ports {sdram_clk}]
create_clock -period 2.500 -name rx_cal_clk [get_ports {rx_cal_clk}]
create_clock -period 2.500 -name tx_cal_clk [get_ports {tx_cal_clk}]
create_clock -period 10.000 -name clk_100m [get_ports {clk_100m}]
create_clock -period 13.468 -name vid_clk_dvi2 [get_ports {vid_clk_dvi2}]
create_clock -period 6.734 -name hdmi_tx_slow_clk [get_ports {hdmi_tx_slow_clk}]
create_clock -period 18.519 -name fb [get_ports {fb}]
create_clock -waveform {0.337 1.010} -period 1.347 -name hdmi_tx_fast_clk [get_ports {hdmi_tx_fast_clk}]
create_clock -period 5.000 -name cpu_clk200 [get_ports {cpu_clk200}]
create_clock -period 10.000 -name core_clk [get_ports {core_clk}]
create_clock -waveform {1.000 2.000} -period 2.000 -name i_mipi_txc_sclk [get_ports {i_mipi_txc_sclk}]
create_clock -waveform {0.500 1.500} -period 2.000 -name i_mipi_txd_sclk [get_ports {i_mipi_txd_sclk}]
create_clock -period 8.000 -name i_mipi_tx_pclk [get_ports {i_mipi_tx_pclk}]
create_clock -period 20.000 -name i_mipi_rx_pclk [get_ports {i_mipi_rx_pclk}]
create_clock -period 200.000 -name CLK_5M [get_ports {CLK_5M}]


create_clock -period 10.000 -name i_cam_ck_CLKOUT [get_ports {i_cam_ck_CLKOUT}]

set_clock_groups -exclusive  -group {i_mipi_rx_pclk} -group {jtag_inst1_TCK}
set_clock_groups -exclusive -group {core_clk} -group {tx_cal_clk} -group {i_mipi_tx_pclk} -group {vid_clk_dvi2} -group {sdram_clk} -group {rx_cal_clk}
set_clock_groups -exclusive -group {i_mipi_tx_pclk}    -group {i_mipi_rx_pclk}

# DC_FIFO CDC ingress exceptions.  These are limited to the first receiving
# synchronizer stage (or its asynchronous reset pin) in the active scanout
# FIFO.  The second stage, local reset release logic, Gray-pointer arithmetic,
# and ordinary data paths remain under normal timing analysis.
set_false_path -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/*ResetReq*~FF|SR}]
set_false_path -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/WrReadyToRd*~FF|SR}]
set_false_path -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.fifo_rd_settle*~FF|SR}]
set_false_path -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.fifo_rd_ready_local~FF|SR}]
set_false_path -from [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdResetReqMeta~FF|CLK}] -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdReadyToWrMeta~FF|D}]
set_false_path -from [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdResetReqSync~FF|CLK}] -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdReadyToWrMeta~FF|D}]
set_false_path -from [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/WrResetReqToRd~FF|CLK}] -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdReadyToWrMeta~FF|D}]
set_false_path -from [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdResetReqMeta~FF|CLK}] -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdResetReqToWrMeta~FF|D}]
set_false_path -from [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdResetReqSync~FF|CLK}] -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/RdResetReqToWrMeta~FF|D}]
set_false_path -from [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/FifoRdAddr*~FF|CLK}] -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.u_rd_fifo/WrRdAddr_meta*~FF|D}]
# The scanout-side FIFO reset completion is a stable one-bit handshake; only
# its first core-domain receiving stage is exempted.
set_false_path -from [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.fifo_rd_ready_local~FF|CLK}] -to [get_pins {checker0/u_ddr_buffer/ddr_rd_buffer_inst/safe_reader.fifo_rd_ready0~FF|D}]

#set_clock_groups -exclusive -group {i_sysclk} -group {jtag_inst1_TCK}                       2.500           -0.197           (R-R)

#set_clock_groups -exclusive -group {i_sysclk_div_2} -group {jtag_inst1_TCK}

#set_clock_groups -exclusive -group {jtag_inst1_TCK} -group {i_hbramClk_fb i_hbramClk_cal i_hbramClk_cal i_hbramClk90} -group {io_systemClk} -group {i_mipi_rx_pclk} -group {i_mipi_tx_pclk i_mipi_clk i_mipi_txc_sclk i_mipi_txd_sclk}

# GPIO Constraints
####################

# JTAG Constraints
####################
create_clock -period 100.000 -name jtag_inst1_TCK [get_ports {jtag_inst1_TCK}]
set_output_delay -clock jtag_inst1_TCK -max 1.519 [get_ports {jtag_inst1_TDO}]
set_output_delay -clock jtag_inst1_TCK -min 0.495 [get_ports {jtag_inst1_TDO}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -max 0.144 [get_ports {jtag_inst1_CAPTURE}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -min 0.077 [get_ports {jtag_inst1_CAPTURE}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -max 0.759 [get_ports {jtag_inst1_RESET}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -min 0.401 [get_ports {jtag_inst1_RESET}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -max 0.234 [get_ports {jtag_inst1_SEL}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -min 0.025 [get_ports {jtag_inst1_SEL}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -max 0.176 [get_ports {jtag_inst1_UPDATE}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -min 0.074 [get_ports {jtag_inst1_UPDATE}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -max 0.197 [get_ports {jtag_inst1_SHIFT}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -min 0.055 [get_ports {jtag_inst1_SHIFT}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -max 1.190 [get_ports {jtag_inst1_DRCK}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -min 0.620 [get_ports {jtag_inst1_DRCK}]
# JTAG Constraints (extra... not used by current Efinity debug tools)
# create_clock -period <USER_PERIOD> -name jtag_inst1_DRCK [get_ports {jtag_inst1_DRCK}]
# set_input_delay -clock_fall -clock jtag_inst1_TCK -max 0.169 [get_ports {jtag_inst1_RUNTEST}]
# set_input_delay -clock_fall -clock jtag_inst1_TCK -min 0.066 [get_ports {jtag_inst1_RUNTEST}]
# Create separate clock groups for JTAG clocks. Remove DRCK clock from the list below if it is not defined.
# set_clock_groups -asynchronous -group {jtag_inst1_TCK jtag_inst1_DRCK}
# JTAG Constraints (extra... not used by current Efinity debug tools)
# Create separate clock groups for JTAG clocks. Remove DRCK clock from the list below if it is not defined.


# HSIO GPIO Constraints
#########################
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~95}] -max 0.302 [get_ports {ddr_addr[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~95}] -min -0.140 [get_ports {ddr_addr[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~36}] -max 0.302 [get_ports {ddr_addr[1]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~36}] -min -0.140 [get_ports {ddr_addr[1]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~70}] -max 0.302 [get_ports {ddr_addr[2]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~70}] -min -0.140 [get_ports {ddr_addr[2]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~50~1}] -max 0.302 [get_ports {ddr_addr[3]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~50~1}] -min -0.140 [get_ports {ddr_addr[3]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~25}] -max 0.302 [get_ports {ddr_addr[4]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~25}] -min -0.140 [get_ports {ddr_addr[4]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~142~1}] -max 0.302 [get_ports {ddr_addr[5]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~142~1}] -min -0.140 [get_ports {ddr_addr[5]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~59}] -max 0.302 [get_ports {ddr_addr[6]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~59}] -min -0.140 [get_ports {ddr_addr[6]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~110}] -max 0.302 [get_ports {ddr_addr[7]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~110}] -min -0.140 [get_ports {ddr_addr[7]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~26}] -max 0.302 [get_ports {ddr_addr[8]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~26}] -min -0.140 [get_ports {ddr_addr[8]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~127}] -max 0.302 [get_ports {ddr_addr[9]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~127}] -min -0.140 [get_ports {ddr_addr[9]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~138}] -max 0.302 [get_ports {ddr_addr[10]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~138}] -min -0.140 [get_ports {ddr_addr[10]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~60}] -max 0.302 [get_ports {ddr_addr[11]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~60}] -min -0.140 [get_ports {ddr_addr[11]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~35}] -max 0.302 [get_ports {ddr_addr[12]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~35}] -min -0.140 [get_ports {ddr_addr[12]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~71}] -max 0.302 [get_ports {ddr_addr[13]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~71}] -min -0.140 [get_ports {ddr_addr[13]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~109}] -max 0.302 [get_ports {ddr_addr[14]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~109}] -min -0.140 [get_ports {ddr_addr[14]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~143~1}] -max 0.302 [get_ports {ddr_addr[15]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~143~1}] -min -0.140 [get_ports {ddr_addr[15]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~74~1}] -max 0.302 [get_ports {ddr_ba[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~74~1}] -min -0.140 [get_ports {ddr_ba[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~132~1}] -max 0.302 [get_ports {ddr_ba[1]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~132~1}] -min -0.140 [get_ports {ddr_ba[1]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~126}] -max 0.302 [get_ports {ddr_ba[2]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~126}] -min -0.140 [get_ports {ddr_ba[2]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~49}] -max 0.302 [get_ports {ddr_cas_n}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~49}] -min -0.140 [get_ports {ddr_cas_n}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~149}] -max 0.302 [get_ports {ddr_ck_lo ddr_ck_hi}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~149}] -min -0.140 [get_ports {ddr_ck_lo ddr_ck_hi}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~123~1}] -max 0.302 [get_ports {ddr_cke[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~123~1}] -min -0.140 [get_ports {ddr_cke[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~95~1}] -max 0.302 [get_ports {ddr_cs_n[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~95~1}] -min -0.140 [get_ports {ddr_cs_n[0]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~131~1}] -max 0.302 [get_ports {ddr_dm_lo[0] ddr_dm_hi[0]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~131~1}] -min -0.140 [get_ports {ddr_dm_lo[0] ddr_dm_hi[0]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~83~1}] -max 0.302 [get_ports {ddr_dm_lo[1] ddr_dm_hi[1]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~83~1}] -min -0.140 [get_ports {ddr_dm_lo[1] ddr_dm_hi[1]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~73~1}] -max 0.302 [get_ports {ddr_odt[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~73~1}] -min -0.140 [get_ports {ddr_odt[0]}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~48}] -max 0.302 [get_ports {ddr_ras_n}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~48}] -min -0.140 [get_ports {ddr_ras_n}]
# set_output_delay -clock <CLOCK> [-reference_pin <clkout_pad>] -max <MAX CALCULATION> [get_ports {ddr_reset_n}]
# set_output_delay -clock <CLOCK> [-reference_pin <clkout_pad>] -min <MIN CALCULATION> [get_ports {ddr_reset_n}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~94}] -max 0.302 [get_ports {ddr_we_n}]
set_output_delay -clock sdram_clk -reference_pin [get_ports {sdram_clk~CLKOUT~218~94}] -min -0.140 [get_ports {ddr_we_n}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~148~1}] -max 0.476 [get_ports {ddr_dq_in_lo[0] ddr_dq_in_hi[0]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~148~1}] -min 0.276 [get_ports {ddr_dq_in_lo[0] ddr_dq_in_hi[0]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~150~1}] -max 0.302 [get_ports {ddr_dq_out_lo[0] ddr_dq_out_hi[0]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~150~1}] -min -0.140 [get_ports {ddr_dq_out_lo[0] ddr_dq_out_hi[0]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~150~1}] -max 0.302 [get_ports {ddr_dq_oe[0]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~150~1}] -min -0.140 [get_ports {ddr_dq_oe[0]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~157~1}] -max 0.476 [get_ports {ddr_dq_in_lo[1] ddr_dq_in_hi[1]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~157~1}] -min 0.276 [get_ports {ddr_dq_in_lo[1] ddr_dq_in_hi[1]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~159~1}] -max 0.302 [get_ports {ddr_dq_out_lo[1] ddr_dq_out_hi[1]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~159~1}] -min -0.140 [get_ports {ddr_dq_out_lo[1] ddr_dq_out_hi[1]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~159~1}] -max 0.302 [get_ports {ddr_dq_oe[1]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~159~1}] -min -0.140 [get_ports {ddr_dq_oe[1]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~164~1}] -max 0.476 [get_ports {ddr_dq_in_lo[2] ddr_dq_in_hi[2]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~164~1}] -min 0.276 [get_ports {ddr_dq_in_lo[2] ddr_dq_in_hi[2]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~166~1}] -max 0.302 [get_ports {ddr_dq_out_lo[2] ddr_dq_out_hi[2]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~166~1}] -min -0.140 [get_ports {ddr_dq_out_lo[2] ddr_dq_out_hi[2]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~166~1}] -max 0.302 [get_ports {ddr_dq_oe[2]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~166~1}] -min -0.140 [get_ports {ddr_dq_oe[2]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~149~1}] -max 0.476 [get_ports {ddr_dq_in_lo[3] ddr_dq_in_hi[3]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~149~1}] -min 0.276 [get_ports {ddr_dq_in_lo[3] ddr_dq_in_hi[3]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~151~1}] -max 0.302 [get_ports {ddr_dq_out_lo[3] ddr_dq_out_hi[3]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~151~1}] -min -0.140 [get_ports {ddr_dq_out_lo[3] ddr_dq_out_hi[3]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~151~1}] -max 0.302 [get_ports {ddr_dq_oe[3]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~151~1}] -min -0.140 [get_ports {ddr_dq_oe[3]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~189~1}] -max 0.476 [get_ports {ddr_dq_in_lo[4] ddr_dq_in_hi[4]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~189~1}] -min 0.276 [get_ports {ddr_dq_in_lo[4] ddr_dq_in_hi[4]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~191~1}] -max 0.302 [get_ports {ddr_dq_out_lo[4] ddr_dq_out_hi[4]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~191~1}] -min -0.140 [get_ports {ddr_dq_out_lo[4] ddr_dq_out_hi[4]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~191~1}] -max 0.302 [get_ports {ddr_dq_oe[4]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~191~1}] -min -0.140 [get_ports {ddr_dq_oe[4]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~156~1}] -max 0.476 [get_ports {ddr_dq_in_lo[5] ddr_dq_in_hi[5]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~156~1}] -min 0.276 [get_ports {ddr_dq_in_lo[5] ddr_dq_in_hi[5]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~158~1}] -max 0.302 [get_ports {ddr_dq_out_lo[5] ddr_dq_out_hi[5]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~158~1}] -min -0.140 [get_ports {ddr_dq_out_lo[5] ddr_dq_out_hi[5]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~158~1}] -max 0.302 [get_ports {ddr_dq_oe[5]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~158~1}] -min -0.140 [get_ports {ddr_dq_oe[5]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~165~1}] -max 0.476 [get_ports {ddr_dq_in_lo[6] ddr_dq_in_hi[6]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~165~1}] -min 0.276 [get_ports {ddr_dq_in_lo[6] ddr_dq_in_hi[6]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~167~1}] -max 0.302 [get_ports {ddr_dq_out_lo[6] ddr_dq_out_hi[6]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~167~1}] -min -0.140 [get_ports {ddr_dq_out_lo[6] ddr_dq_out_hi[6]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~167~1}] -max 0.302 [get_ports {ddr_dq_oe[6]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~167~1}] -min -0.140 [get_ports {ddr_dq_oe[6]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~188~1}] -max 0.476 [get_ports {ddr_dq_in_lo[7] ddr_dq_in_hi[7]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~188~1}] -min 0.276 [get_ports {ddr_dq_in_lo[7] ddr_dq_in_hi[7]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~190~1}] -max 0.302 [get_ports {ddr_dq_out_lo[7] ddr_dq_out_hi[7]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~190~1}] -min -0.140 [get_ports {ddr_dq_out_lo[7] ddr_dq_out_hi[7]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~190~1}] -max 0.302 [get_ports {ddr_dq_oe[7]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~190~1}] -min -0.140 [get_ports {ddr_dq_oe[7]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~82~1}] -max 0.476 [get_ports {ddr_dq_in_lo[8] ddr_dq_in_hi[8]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~82~1}] -min 0.276 [get_ports {ddr_dq_in_lo[8] ddr_dq_in_hi[8]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~84~1}] -max 0.302 [get_ports {ddr_dq_out_lo[8] ddr_dq_out_hi[8]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~84~1}] -min -0.140 [get_ports {ddr_dq_out_lo[8] ddr_dq_out_hi[8]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~84~1}] -max 0.302 [get_ports {ddr_dq_oe[8]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~84~1}] -min -0.140 [get_ports {ddr_dq_oe[8]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~39~1}] -max 0.476 [get_ports {ddr_dq_in_lo[9] ddr_dq_in_hi[9]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~39~1}] -min 0.276 [get_ports {ddr_dq_in_lo[9] ddr_dq_in_hi[9]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~41~1}] -max 0.302 [get_ports {ddr_dq_out_lo[9] ddr_dq_out_hi[9]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~41~1}] -min -0.140 [get_ports {ddr_dq_out_lo[9] ddr_dq_out_hi[9]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~41~1}] -max 0.302 [get_ports {ddr_dq_oe[9]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~41~1}] -min -0.140 [get_ports {ddr_dq_oe[9]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~22~1}] -max 0.476 [get_ports {ddr_dq_in_lo[10] ddr_dq_in_hi[10]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~22~1}] -min 0.276 [get_ports {ddr_dq_in_lo[10] ddr_dq_in_hi[10]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~24~1}] -max 0.302 [get_ports {ddr_dq_out_lo[10] ddr_dq_out_hi[10]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~24~1}] -min -0.140 [get_ports {ddr_dq_out_lo[10] ddr_dq_out_hi[10]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~24~1}] -max 0.302 [get_ports {ddr_dq_oe[10]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~24~1}] -min -0.140 [get_ports {ddr_dq_oe[10]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~92~1}] -max 0.476 [get_ports {ddr_dq_in_lo[11] ddr_dq_in_hi[11]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~92~1}] -min 0.276 [get_ports {ddr_dq_in_lo[11] ddr_dq_in_hi[11]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~94~1}] -max 0.302 [get_ports {ddr_dq_out_lo[11] ddr_dq_out_hi[11]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~94~1}] -min -0.140 [get_ports {ddr_dq_out_lo[11] ddr_dq_out_hi[11]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~94~1}] -max 0.302 [get_ports {ddr_dq_oe[11]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~94~1}] -min -0.140 [get_ports {ddr_dq_oe[11]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~23~1}] -max 0.476 [get_ports {ddr_dq_in_lo[12] ddr_dq_in_hi[12]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~23~1}] -min 0.276 [get_ports {ddr_dq_in_lo[12] ddr_dq_in_hi[12]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~25~1}] -max 0.302 [get_ports {ddr_dq_out_lo[12] ddr_dq_out_hi[12]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~25~1}] -min -0.140 [get_ports {ddr_dq_out_lo[12] ddr_dq_out_hi[12]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~25~1}] -max 0.302 [get_ports {ddr_dq_oe[12]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~25~1}] -min -0.140 [get_ports {ddr_dq_oe[12]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~38~1}] -max 0.476 [get_ports {ddr_dq_in_lo[13] ddr_dq_in_hi[13]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~38~1}] -min 0.276 [get_ports {ddr_dq_in_lo[13] ddr_dq_in_hi[13]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~40~1}] -max 0.302 [get_ports {ddr_dq_out_lo[13] ddr_dq_out_hi[13]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~40~1}] -min -0.140 [get_ports {ddr_dq_out_lo[13] ddr_dq_out_hi[13]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~40~1}] -max 0.302 [get_ports {ddr_dq_oe[13]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~40~1}] -min -0.140 [get_ports {ddr_dq_oe[13]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~11~1}] -max 0.476 [get_ports {ddr_dq_in_lo[14] ddr_dq_in_hi[14]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~11~1}] -min 0.276 [get_ports {ddr_dq_in_lo[14] ddr_dq_in_hi[14]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~13~1}] -max 0.302 [get_ports {ddr_dq_out_lo[14] ddr_dq_out_hi[14]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~13~1}] -min -0.140 [get_ports {ddr_dq_out_lo[14] ddr_dq_out_hi[14]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~13~1}] -max 0.302 [get_ports {ddr_dq_oe[14]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~13~1}] -min -0.140 [get_ports {ddr_dq_oe[14]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~12~1}] -max 0.476 [get_ports {ddr_dq_in_lo[15] ddr_dq_in_hi[15]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~12~1}] -min 0.276 [get_ports {ddr_dq_in_lo[15] ddr_dq_in_hi[15]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~14~1}] -max 0.302 [get_ports {ddr_dq_out_lo[15] ddr_dq_out_hi[15]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~14~1}] -min -0.140 [get_ports {ddr_dq_out_lo[15] ddr_dq_out_hi[15]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~14~1}] -max 0.302 [get_ports {ddr_dq_oe[15]}]
set_output_delay -clock tx_cal_clk_90edge -reference_pin [get_ports {tx_cal_clk_90edge~CLKOUT~14~1}] -min -0.140 [get_ports {ddr_dq_oe[15]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~172~1}] -max 0.476 [get_ports {ddr_dqs_in_lo[0] ddr_dqs_in_hi[0]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~172~1}] -min 0.276 [get_ports {ddr_dqs_in_lo[0] ddr_dqs_in_hi[0]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~174~1}] -max 0.302 [get_ports {ddr_dqs_out_lo[0] ddr_dqs_out_hi[0]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~174~1}] -min -0.140 [get_ports {ddr_dqs_out_lo[0] ddr_dqs_out_hi[0]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~174~1}] -max 0.302 [get_ports {ddr_dqs_oe[0]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~174~1}] -min -0.140 [get_ports {ddr_dqs_oe[0]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~175~1}] -max 0.302 [get_ports {ddr_dqs_oe_n[0]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~175~1}] -min -0.140 [get_ports {ddr_dqs_oe_n[0]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~30~1}] -max 0.476 [get_ports {ddr_dqs_in_lo[1] ddr_dqs_in_hi[1]}]
set_input_delay -clock rx_cal_clk -reference_pin [get_ports {rx_cal_clk~CLKOUT~30~1}] -min 0.276 [get_ports {ddr_dqs_in_lo[1] ddr_dqs_in_hi[1]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~32~1}] -max 0.302 [get_ports {ddr_dqs_out_lo[1] ddr_dqs_out_hi[1]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~32~1}] -min -0.140 [get_ports {ddr_dqs_out_lo[1] ddr_dqs_out_hi[1]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~32~1}] -max 0.302 [get_ports {ddr_dqs_oe[1]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~32~1}] -min -0.140 [get_ports {ddr_dqs_oe[1]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~33~1}] -max 0.302 [get_ports {ddr_dqs_oe_n[1]}]
set_output_delay -clock tx_cal_clk -reference_pin [get_ports {tx_cal_clk~CLKOUT~33~1}] -min -0.140 [get_ports {ddr_dqs_oe_n[1]}]

# LVDS Tx Constraints
#######################
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~175}] -max 0.435 [get_ports {tmds_tx_clk_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~175}] -min -0.140 [get_ports {tmds_tx_clk_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~175}] -max 0.435 [get_ports {tmds_tx_clk_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~175}] -min -0.140 [get_ports {tmds_tx_clk_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~175}] -max 0.483 [get_ports {tmds_tx_clk_TX_RST}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~175}] -min -0.175 [get_ports {tmds_tx_clk_TX_RST}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~215}] -max 0.435 [get_ports {tmds_tx_data0_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~215}] -min -0.140 [get_ports {tmds_tx_data0_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~215}] -max 0.435 [get_ports {tmds_tx_data0_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~215}] -min -0.140 [get_ports {tmds_tx_data0_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~215}] -max 0.483 [get_ports {tmds_tx_data0_TX_RST}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~215}] -min -0.175 [get_ports {tmds_tx_data0_TX_RST}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~187}] -max 0.435 [get_ports {tmds_tx_data1_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~187}] -min -0.140 [get_ports {tmds_tx_data1_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~187}] -max 0.435 [get_ports {tmds_tx_data1_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~187}] -min -0.140 [get_ports {tmds_tx_data1_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~187}] -max 0.483 [get_ports {tmds_tx_data1_TX_RST}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~187}] -min -0.175 [get_ports {tmds_tx_data1_TX_RST}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~200}] -max 0.435 [get_ports {tmds_tx_data2_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~200}] -min -0.140 [get_ports {tmds_tx_data2_TX_DATA[*]}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~200}] -max 0.435 [get_ports {tmds_tx_data2_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~200}] -min -0.140 [get_ports {tmds_tx_data2_TX_OE}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~200}] -max 0.483 [get_ports {tmds_tx_data2_TX_RST}]
set_output_delay -clock hdmi_tx_slow_clk -reference_pin [get_ports {hdmi_tx_slow_clk~CLKOUT~218~200}] -min -0.175 [get_ports {tmds_tx_data2_TX_RST}]

# MIPI RX Lane Constraints
############################
# 
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -max 0.604 [get_ports {cam_d0_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -min 0.070 [get_ports {cam_d0_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -max 0.362 [get_ports {cam_d0_RST}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -min -0.140 [get_ports {cam_d0_RST}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -max 0.725 [get_ports {cam_d0_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -min 0.420 [get_ports {cam_d0_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -max 0.560 [get_ports {cam_d0_HS_IN[*]}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~82~322}] -min 0.325 [get_ports {cam_d0_HS_IN[*]}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -max 0.604 [get_ports {cam_d1_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -min 0.070 [get_ports {cam_d1_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -max 0.362 [get_ports {cam_d1_RST}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -min -0.140 [get_ports {cam_d1_RST}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -max 0.725 [get_ports {cam_d1_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -min 0.420 [get_ports {cam_d1_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -max 0.560 [get_ports {cam_d1_HS_IN[*]}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~130~322}] -min 0.325 [get_ports {cam_d1_HS_IN[*]}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -max 0.604 [get_ports {cam_d2_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -min 0.070 [get_ports {cam_d2_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -max 0.362 [get_ports {cam_d2_RST}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -min -0.140 [get_ports {cam_d2_RST}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -max 0.725 [get_ports {cam_d2_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -min 0.420 [get_ports {cam_d2_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -max 0.560 [get_ports {cam_d2_HS_IN[*]}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~73~322}] -min 0.325 [get_ports {cam_d2_HS_IN[*]}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -max 0.604 [get_ports {cam_d3_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -min 0.070 [get_ports {cam_d3_FIFO_RD}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -max 0.362 [get_ports {cam_d3_RST}]
set_output_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -min -0.140 [get_ports {cam_d3_RST}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -max 0.725 [get_ports {cam_d3_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -min 0.420 [get_ports {cam_d3_FIFO_EMPTY}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -max 0.560 [get_ports {cam_d3_HS_IN[*]}]
set_input_delay -clock i_cam_ck_CLKOUT -reference_pin [get_ports {i_cam_ck_CLKOUT~CLKOUT~141~322}] -min 0.325 [get_ports {cam_d3_HS_IN[*]}]

# MIPI TX Lane Constraints
############################
set_output_delay -clock i_mipi_txd_sclk -reference_pin [get_ports {i_mipi_txd_sclk~CLKOUT~39~322}] -max 0.435 [get_ports {mipi_dp_clk_HS_OUT[*]}]
set_output_delay -clock i_mipi_txd_sclk -reference_pin [get_ports {i_mipi_txd_sclk~CLKOUT~39~322}] -min -0.140 [get_ports {mipi_dp_clk_HS_OUT[*]}]
set_output_delay -clock i_mipi_txd_sclk -reference_pin [get_ports {i_mipi_txd_sclk~CLKOUT~39~322}] -max 0.362 [get_ports {mipi_dp_clk_RST}]
set_output_delay -clock i_mipi_txd_sclk -reference_pin [get_ports {i_mipi_txd_sclk~CLKOUT~39~322}] -min -0.140 [get_ports {mipi_dp_clk_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~63~322}] -max 0.435 [get_ports {mipi_dp_data0_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~63~322}] -min -0.140 [get_ports {mipi_dp_data0_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~63~322}] -max 0.362 [get_ports {mipi_dp_data0_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~63~322}] -min -0.140 [get_ports {mipi_dp_data0_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~71~322}] -max 0.435 [get_ports {mipi_dp_data1_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~71~322}] -min -0.140 [get_ports {mipi_dp_data1_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~71~322}] -max 0.362 [get_ports {mipi_dp_data1_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~71~322}] -min -0.140 [get_ports {mipi_dp_data1_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~55~322}] -max 0.435 [get_ports {mipi_dp_data2_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~55~322}] -min -0.140 [get_ports {mipi_dp_data2_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~55~322}] -max 0.362 [get_ports {mipi_dp_data2_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~55~322}] -min -0.140 [get_ports {mipi_dp_data2_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~31~322}] -max 0.435 [get_ports {mipi_dp_data3_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~31~322}] -min -0.140 [get_ports {mipi_dp_data3_HS_OUT[*]}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~31~322}] -max 0.362 [get_ports {mipi_dp_data3_RST}]
set_output_delay -clock i_mipi_tx_pclk -reference_pin [get_ports {i_mipi_tx_pclk~CLKOUT~31~322}] -min -0.140 [get_ports {mipi_dp_data3_RST}]



# CPU200 constraints R2: official peripheral template uses 10MHz; chosen envelope <=10MHz.
# CLK25/27 are physical PLL references, not a claim that CPU/core are asynchronous.
create_clock -period 40.000 -name CLK_25M [get_ports {CLK_25M}]
create_clock -period 37.037 -name CLK_27M [get_ports {CLK_27M}]
# FT4232H t4 + Ti60 JTAG_IN + explicit external relative skew/jitter +/-10ns.
set_input_delay -clock_fall -clock jtag_inst1_TCK -min -7.495 [get_ports {jtag_inst1_TDI}]
set_input_delay -clock_fall -clock jtag_inst1_TCK -max 19.972 [get_ports {jtag_inst1_TDI}]
# CPU PLL reset is an asynchronous hard-macro port, not a synchronous data output.
# External contract: low >=100ns, stable CLK25 before release; macro RSTN >=10ns.
set_max_delay -from [get_ports {i_arstn}] -to [get_ports {cpu_pll_reset}] 10.000
set_min_delay -from [get_ports {i_arstn}] -to [get_ports {cpu_pll_reset}] 0.000
set_false_path -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/flowCCUnsafeByToggle_1/inputArea_target~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/flowCCUnsafeByToggle_1/inputArea_target_buffercc/buffers_0~FF|D}]
set_false_path -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_toggle~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_toggle_sync[0]~FF|D}]
set_false_path -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_seen~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_ack_sync[0]~FF|D}]
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/flowCCUnsafeByToggle_1/inputArea_data_last~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge_io_remote_cmd_payload_last~FF|D}] 5.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/flowCCUnsafeByToggle_1/inputArea_data_fragment[0]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge_io_remote_cmd_payload_fragment[0]~FF|D}] 5.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[1]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[1]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[2]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[2]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[3]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[3]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[4]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[4]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[5]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[5]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[6]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[6]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[7]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[7]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[8]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[8]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[9]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[9]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[10]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[10]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[11]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[11]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[12]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[12]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[13]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[13]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[14]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[14]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[15]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[15]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[16]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[16]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[17]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[17]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[18]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[18]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[19]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[19]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[20]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[20]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[21]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[21]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[22]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[22]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[23]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[23]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[24]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[24]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[25]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[25]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[26]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[26]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[27]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[27]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[28]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[28]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[29]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[29]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[30]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[30]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[31]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[31]~FF|D}] 100.000
set_max_delay -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_mailbox_payload[32]~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_snapshot[32]~FF|D}] 100.000
set_false_path -from [get_ports {cpu_pll_locked i_pll_locked}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/io_asyncReset_asyncAssertSyncDeassert_buffercc*~FF|SR}]
set_false_path -from [get_ports {cpu_pll_locked i_pll_locked}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/peripheral_raw_reset_sync*~FF|SR}]
set_false_path -from [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/debugCd_logic_outputReset~FF|CLK}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_hardJtag_debug_logic_jtagBridge/rsp_tck_reset_sync*~FF|SR}]
set_false_path -from [get_ports {i_pll_locked pll_locked user_pll_locked}] -to [get_pins {u_riscv_top/asyncResetn_synchronizer/data*~FF|SR}]
set_false_path -from [get_ports {i_pll_locked pll_locked user_pll_locked}] -to [get_pins {u_riscv_top/synchronized_gpio_asyncResetn~FF|SR}]

# control_menu_decor16 (2026-10-29): FIX port constraints for the new key / I2C pads (see scripts/apply_rtl_io.py).
# ADS1115 asynchronous pad inputs, exact first-stage D only; mapped coverage remains unverified until PNR.
set_false_path -from [get_ports {i2c_scl_in}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_i2c_0_io_logic/i2cCtrl/io_i2c_scl_read_buffercc/buffers_0~FF|D}]
set_false_path -from [get_ports {i2c_sda_in}] -to [get_pins {u_riscv_top/soc_inst/u_EfxSapphireSoc/system_i2c_0_io_logic/i2cCtrl/io_i2c_sda_read_buffercc/buffers_0~FF|D}]

# Efinity requires an output reference before register-to-port max/min is accepted.
# Zero reference to existing core_clk only activates OE endpoints; explicit max/min below
# are the 100ns/0ns propagation contract. This is not an external sampling clock.
set_output_delay -clock core_clk -max 0.000 [get_ports {i2c_scl_oe i2c_sda_oe}]
set_output_delay -clock core_clk -min 0.000 [get_ports {i2c_scl_oe i2c_sda_oe}]
# RAW_XY_OE_BUDGET: no fabricated I2C clock; bounded clock-to-pad OE propagation.
# Controller TSU_DATA=63 (630ns), two extra 100MHz output cycles allowed (20ns),
# maximum OE path=100ns, external rise <=300ns => >=210ns data setup (>100ns ADS requirement).
# FW final LOW/TBUF=520 ticks (5.2us), HIGH=500 ticks (5us); software stalls may lengthen SCL.
# OUT is a mapped constant zero; only OE switches. External edges require board verification.
set_max_delay -from [get_pins {u_riscv_top/joy_drive_release[1]~FF|CLK}] -to [get_ports {i2c_scl_oe}] 100.000
set_min_delay -from [get_pins {u_riscv_top/joy_drive_release[1]~FF|CLK}] -to [get_ports {i2c_scl_oe}] 0.000
set_max_delay -from [get_pins {riscv_gpio_write_unused[0]~FF|CLK}] -to [get_ports {i2c_scl_oe}] 100.000
set_min_delay -from [get_pins {riscv_gpio_write_unused[0]~FF|CLK}] -to [get_ports {i2c_scl_oe}] 0.000
set_max_delay -from [get_pins {riscv_gpio_write_enable_unused[0]~FF|CLK}] -to [get_ports {i2c_scl_oe}] 100.000
set_min_delay -from [get_pins {riscv_gpio_write_enable_unused[0]~FF|CLK}] -to [get_ports {i2c_scl_oe}] 0.000
set_max_delay -from [get_pins {u_riscv_top/joy_scl_write~FF|CLK}] -to [get_ports {i2c_scl_oe}] 100.000
set_min_delay -from [get_pins {u_riscv_top/joy_scl_write~FF|CLK}] -to [get_ports {i2c_scl_oe}] 0.000
set_max_delay -from [get_pins {u_riscv_top/joy_drive_release[1]~FF|CLK}] -to [get_ports {i2c_sda_oe}] 100.000
set_min_delay -from [get_pins {u_riscv_top/joy_drive_release[1]~FF|CLK}] -to [get_ports {i2c_sda_oe}] 0.000
set_max_delay -from [get_pins {riscv_gpio_write_unused[0]~FF|CLK}] -to [get_ports {i2c_sda_oe}] 100.000
set_min_delay -from [get_pins {riscv_gpio_write_unused[0]~FF|CLK}] -to [get_ports {i2c_sda_oe}] 0.000
set_max_delay -from [get_pins {riscv_gpio_write_enable_unused[0]~FF|CLK}] -to [get_ports {i2c_sda_oe}] 100.000
set_min_delay -from [get_pins {riscv_gpio_write_enable_unused[0]~FF|CLK}] -to [get_ports {i2c_sda_oe}] 0.000
set_max_delay -from [get_pins {u_riscv_top/joy_sda_write~FF|CLK}] -to [get_ports {i2c_sda_oe}] 100.000
set_min_delay -from [get_pins {u_riscv_top/joy_sda_write~FF|CLK}] -to [get_ports {i2c_sda_oe}] 0.000
set_false_path -from [get_ports {cpu_pll_locked i_pll_locked}] -to [get_pins {u_riscv_top/joy_drive_release[0]~FF|SR}]
set_false_path -from [get_ports {cpu_pll_locked i_pll_locked}] -to [get_pins {u_riscv_top/joy_drive_release[1]~FF|SR}]

# Board keys: asynchronous human inputs, only their first receiving D pins.
set_false_path -from [get_ports {key1_n}] -to [get_pins {u_riscv_top/u_board_keys_sync/key_meta[0]~FF|D}]
set_false_path -from [get_ports {key2_n}] -to [get_pins {u_riscv_top/u_board_keys_sync/key_meta[1]~FF|D}]
set_false_path -from [get_ports {key3_n}] -to [get_pins {u_riscv_top/u_board_keys_sync/key_meta[2]~FF|D}]

# ---- GHD3 CDC: 224-bit HUD snapshot mailbox rtl/q3_perf_snapshot_cdc.v (core_clk 25 MHz-ref PLL -> hdmi_tx_slow_clk 27 MHz-ref PLL;
# different crystals, so the domains are truly asynchronous). Bundled data: publish_data_core is loaded and req toggled on the same
# core edge and held until ack returns; the pixel side samples req through req_sync0/req_sync1 and loads the whole bundle only
# when req_sync1 != req_seen at a vsync rising edge, i.e. at least 2 pixel periods (13.468 ns) after req reaches req_sync0.
# Requirement: bundle skew relative to req < 2 pixel periods - setup. Constraint: every data bit within 1 pixel period (6.734 ns),
# req and the reset assertion likewise, ack within 1 core period. This replaces the meaningless nearest-edge (2 ps) check with
# the real requirement; it is checked by STA (max_delay), not cut (no false path). Random-phase simulation:
# validation/gold_hud_dualelf_20261030/sim/run_snapshot_cdc.py (skew up to 1.5 periods clean, 3 periods detected as torn).
set_max_delay -from [get_pins {u_q3_perf_snapshot_cdc/publish_data_core[*]~FF|CLK}] -to [get_pins {panel_snapshot_pix[*]~FF|D}] 6.734
set_max_delay -from [get_pins {u_q3_perf_snapshot_cdc/req_toggle_core~FF|CLK}] -to [get_pins {u_q3_perf_snapshot_cdc/req_sync0_pix~FF|D}] 6.734
# ack_toggle_pix and req_seen_pix carry the same value and are merged by synthesis (either name may survive)
set_max_delay -from [get_pins {u_q3_perf_snapshot_cdc/req_seen_pix~FF|CLK}] -to [get_pins {u_q3_perf_snapshot_cdc/ack_sync0_core~FF|D}] 10.000
set_max_delay -from [get_pins {u_q3_perf_snapshot_cdc/ack_toggle_pix~FF|CLK}] -to [get_pins {u_q3_perf_snapshot_cdc/ack_sync0_core~FF|D}] 10.000
# asynchronous assertion of the pixel-side reset synchronizer (deassertion is synchronous to the pixel clock)
set_max_delay -from [get_pins {cal_done~FF|CLK}] -to [get_pins {u_q3_perf_snapshot_cdc/pix_reset_sync[*]~FF|SR}] 6.734
