//`define DEBUG

module nisetro_wsc
(
	// for debugging
`ifdef DEBUG
	DEBUG_OUT,

	DEBUG_DCLK_OUT,
	DEBUG_BCLK_OUT,
`endif

	// fx2 signals
	FX2_IFCLK, FX2_CLKOUT,
	FX2_SLOE, FX2_SLRD, FX2_SLWR,
	FX2_FD, FX2_FIFOADDR,
	FX2_FLAGA, FX2_FLAGB, FX2_FLAGC,
	FX2_PKTEND,

	// max2 control signals
	MAX2_MODE, MAX2_REG_ADDR, MAX2_RESET, MAX2_FIFO_DIR,

	// wsc signals
	WSC_BCLK, WSC_DCLK1,
	WSC_HSYNC, WSC_VSYNC, WSC_RGB,
	WSC_RESET, WSC_LRCK, WSC_SDAT,
	WSC_EXT_SOUND_ENABLE
);

`ifdef DEBUG
output wire [7:0] DEBUG_OUT;
output wire DEBUG_BCLK_OUT;
output wire DEBUG_DCLK_OUT;

assign DEBUG_OUT[0] = WSC_nRESET;
assign DEBUG_OUT[1] = MAX2_RESET;
assign DEBUG_OUT[2] = CAPTURE_RESET;
assign DEBUG_OUT[3] = AUDIO_CAPTURE_ENABLED;
assign DEBUG_OUT[4] = WSC_LRCK;
assign DEBUG_OUT[5] = WSC_SDAT;

assign DEBUG_BCLK_OUT = WSC_BCLK;
assign DEBUG_DCLK_OUT = DCLK;
`endif

`define FRAME_LINE_START		4
`define FRAME_LINE_END			`FRAME_LINE_START + 4
`define LINE_PIXEL_START		6
`define LINE_PIXEL_END			`LINE_PIXEL_START + 240

// 1 for 40ns, 2 for 60ns delay
`define BCLK_DELAY_COUNT		2

`define MAX2_REG_ADDR_BITS		3
`define MAX2_MODE_START			0
`define MAX2_MODE_REG_READ		1
`define MAX2_MODE_REG_WRITE	2
`define MAX2_MODE_IDLE			3

`define DIR_FX2_TO_PC	0
`define DIR_PC_TO_FX2	1

`define CAPTURE_BUFFER_BITS	2
`define CAPTURE_BUFFER_COUNT	`CAPTURE_BUFFER_BITS'b11

`define AUDIO_CAPTURE_BUFFER_BITS	2
`define AUDIO_CAPTURE_BUFFER_COUNT	`AUDIO_CAPTURE_BUFFER_BITS'b11

input wire FX2_IFCLK;
input wire FX2_CLKOUT;
input wire FX2_FLAGA;
input wire FX2_FLAGB;
input wire FX2_FLAGC;
output wire FX2_SLOE;
output wire FX2_SLRD;
output wire FX2_SLWR;
output wire [1:0] FX2_FIFOADDR;
inout wire [7:0] FX2_FD;
output wire FX2_PKTEND;

input wire [`MAX2_REG_ADDR_BITS - 1:0] MAX2_REG_ADDR;
input wire [1:0] MAX2_MODE;
input wire MAX2_RESET;
input wire MAX2_FIFO_DIR;

inout wire WSC_EXT_SOUND_ENABLE;
input wire WSC_RESET;
input wire WSC_BCLK;
input wire WSC_DCLK1;
input wire WSC_HSYNC;
input wire WSC_VSYNC;
input wire WSC_LRCK;
input wire WSC_SDAT;
input wire [11:0] WSC_RGB;

wire WSC_nVSYNC, WSC_nRESET;
assign WSC_nVSYNC = ~WSC_VSYNC;
assign WSC_nRESET = ~WSC_RESET;

wire CAPTURE_RESET;
assign CAPTURE_RESET = (MAX2_RESET | WSC_nRESET);

reg ext_sound_enabled = 1'b1;
assign WSC_EXT_SOUND_ENABLE = (ext_sound_enabled == 1'b1 ? 1'b0 : 1'bz);

// buffers for caching pixel data
reg [15:0] capture_buffer [0:`CAPTURE_BUFFER_COUNT];
reg [`CAPTURE_BUFFER_BITS - 1:0] buffer_index = 0;

wire [7:0] max2_reg_read_data;

assign FX2_FIFOADDR = fx2_fifoaddr;

// when starting FIFO reads, need to assert SLOE first
assign FX2_SLOE = ((MAX2_MODE == `MAX2_MODE_START) && (MAX2_FIFO_DIR == `DIR_PC_TO_FX2)) ? 1'b0 : 1'b1;

// nisetro never use FIFO read
assign FX2_SLRD = 1;

// FIFO write enabled by buffer transfering
assign FX2_SLWR = fx2_slwr;

// never commit a packet
assign FX2_PKTEND = 1;

// decide FX2_FD should be input or output
// if doing a reg write or fifo read, makes FX2_FD as input,
// or outputing reg data or fifo write data
assign FX2_FD = (((MAX2_MODE == `MAX2_MODE_REG_WRITE) || 
					  ((MAX2_MODE == `MAX2_MODE_START) && (MAX2_FIFO_DIR == `DIR_PC_TO_FX2))) ? 8'bzzzzzzzz :
					  ((MAX2_MODE == `MAX2_MODE_REG_READ) ? max2_reg_read_data : fifo_data));

// generate DCLK from BCLK
reg [`BCLK_DELAY_COUNT:0] BCLK_Delay;
reg [15:0] BCLKx2_Delay;
reg [7:0] BCLKx4_Delay;
wire dBCLK, dBCLKx2, dBCLKx4, DCLK;
assign dBCLK = BCLK_Delay[`BCLK_DELAY_COUNT];
assign dBCLKx2 = BCLKx2_Delay[15] ^ dBCLK;
assign dBCLKx4 = BCLKx4_Delay[7] ^ dBCLKx2;

assign DCLK = dBCLKx4;

always @(posedge FX2_IFCLK)
begin
	BCLK_Delay <= BCLK_Delay << 1;
	BCLK_Delay[0] <= WSC_BCLK;

	BCLKx2_Delay <= BCLKx2_Delay << 1;
	BCLKx2_Delay[0] <= dBCLK;
	
	BCLKx4_Delay <= BCLKx4_Delay << 1;
	BCLKx4_Delay[0] <= dBCLKx2;
end


// handle max2 register reading/writing
/*
assign max2_reg_read_data = read_reg_data(MAX2_REG_ADDR, MAX2_FIFO_DIR);

always @(posedge FX2_IFCLK)
begin
	if (MAX2_MODE == `MAX2_MODE_REG_WRITE) begin
		// TODO fetch settings from FX2_FD
		//case (MAX2_REG_ADDR)
		//	3'b000	: ext_sound_enabled = FX2_FD[0];
		//endcase
	end
end
*/

// transfer data to fx2 FIFO
reg transfer_buffer_byte_select;
reg [`CAPTURE_BUFFER_BITS - 1:0] transfer_buffer_index = 0;
reg [`CAPTURE_BUFFER_BITS - 1:0] ifclk_synced_buffer_index = 0;

reg [1:0] transfer_audio_buffer_byte_select = 0;
reg [`AUDIO_CAPTURE_BUFFER_BITS - 1:0] transfer_audio_buffer_index = 0;
reg [`AUDIO_CAPTURE_BUFFER_BITS - 1:0] ifclk_synced_audio_buffer_index = 0;

reg [7:0] fifo_data;
reg [1:0] fx2_fifoaddr = 2'b01;
reg fx2_slwr = 1'b1;

always @(posedge FX2_IFCLK or posedge MAX2_RESET)
begin
	if (MAX2_RESET == 1) begin
		// reset all index
		transfer_buffer_byte_select <= 1'b0;
		transfer_audio_buffer_byte_select <= 0;
		
		transfer_buffer_index <= 0;
		transfer_audio_buffer_index <= 0;
		
		ifclk_synced_buffer_index <= 0;
		ifclk_synced_audio_buffer_index <= 0;
	end
	else begin	
		if (MAX2_FIFO_DIR == `DIR_FX2_TO_PC) begin
			// synchronize buffer index
			ifclk_synced_buffer_index <= buffer_index;
			ifclk_synced_audio_buffer_index <= audio_buffer_index;
			
			// IFCLK = 48MHz, PIXEL CLOCK = 3.072MHz, BCLK = 768KHz
			if ((ifclk_synced_audio_buffer_index != transfer_audio_buffer_index) && FX2_FLAGB == 1) begin
				fx2_fifoaddr <= 2'b10;	// select EP6
				fx2_slwr <= 0;	// assert /SLWR
				
				fifo_data <= audio_fifo_write_data(transfer_audio_buffer_byte_select, audio_capture_buffer[transfer_audio_buffer_index]);

				// switch to next transfering byte
				if (transfer_audio_buffer_byte_select == 2'b11) begin
					if (transfer_audio_buffer_index == `AUDIO_CAPTURE_BUFFER_COUNT) begin
						transfer_audio_buffer_index <= 0;
					end
					else begin
						transfer_audio_buffer_index <= transfer_audio_buffer_index + 1'b1;					
					end

					transfer_audio_buffer_byte_select <= 0;
				end
				else begin
					transfer_audio_buffer_byte_select <= transfer_audio_buffer_byte_select + 1'b1;
				end
			end
			else if ((ifclk_synced_buffer_index != transfer_buffer_index) && FX2_FLAGA == 1) begin
				fx2_fifoaddr <= 2'b00;	// select EP2
				fx2_slwr <= 0;	// assert /SLWR

				fifo_data <= fifo_write_data(transfer_buffer_byte_select, capture_buffer[transfer_buffer_index]);
				
				// switch to high/low byte
				transfer_buffer_byte_select <= ~transfer_buffer_byte_select;
				
				if (transfer_buffer_byte_select == 1'b1) begin
					if (transfer_buffer_index == `CAPTURE_BUFFER_COUNT)
						transfer_buffer_index <= 0;
					else
						transfer_buffer_index <= transfer_buffer_index + 1'b1;
				end
			end
			else begin
				// nothing to transfer or EP2 / 6 is full, select EP4 (is disabled)
				fx2_fifoaddr <= 2'b01;
				fx2_slwr <= 1;				
			end			
		end
		else begin
			// TODO for out ep transfer
			fx2_fifoaddr <= 2'b01;
			fx2_slwr <= 1;
		end
	end
end

reg first_frame_arrived = 0;
wire CAPTURE_ENABLED;
assign CAPTURE_ENABLED = first_frame_arrived;

always @(posedge WSC_VSYNC or posedge CAPTURE_RESET)
begin
	if (CAPTURE_RESET == 1) begin
		first_frame_arrived <= 1'b0;
	end
	else begin
		// always start capturing from frame start
		first_frame_arrived <= 1'b1;
	end
end

// capture one pixel and store into buffer
always @(posedge DCLK or posedge CAPTURE_RESET)
begin
	if (CAPTURE_RESET == 1) begin
		// reset buffer index
		buffer_index <= 0;
	end
	else begin
		if (MAX2_FIFO_DIR == `DIR_FX2_TO_PC) begin
			if ((MAX2_MODE == `MAX2_MODE_START) && (CAPTURE_ENABLED == 1)) begin
				begin
					capture_buffer[buffer_index][11:0] <= WSC_RGB;
					capture_buffer[buffer_index][12] <= WSC_SDAT;
					capture_buffer[buffer_index][13] <= WSC_LRCK;
					capture_buffer[buffer_index][14] <= WSC_HSYNC;
					capture_buffer[buffer_index][15] <= WSC_nVSYNC;					
				end
				
				begin
					// wrap buffer index
					if (buffer_index == `CAPTURE_BUFFER_COUNT)
						buffer_index <= 0;
					else
						buffer_index <= buffer_index + 1'b1;
				end				
			end
		end
	end
end

reg first_audio_frame_arrived = 0;
reg [31:0] audio_capture_buffer [0:`AUDIO_CAPTURE_BUFFER_COUNT];
reg [`AUDIO_CAPTURE_BUFFER_BITS - 1: 0] audio_buffer_index = 0;
reg [4:0] audio_buffer_bit_index = 0;

wire AUDIO_CAPTURE_ENABLED;
assign AUDIO_CAPTURE_ENABLED = first_audio_frame_arrived;

/*
// FIXME when playing WS compatible WSC games, there will be noise on right channel...
// for WS only or WSC only games, there is no noise...
always @(posedge WSC_LRCK or posedge CAPTURE_RESET)
begin
	if (CAPTURE_RESET == 1) begin
		first_audio_frame_arrived <= 1'b0;

		// reset index when MAX2 is reset
		// so the transfer index will keep counting in order
		if (MAX2_RESET == 1)
			audio_buffer_index <= 0;

	end
	else begin
		first_audio_frame_arrived <= 1'b1;
		
		if (MAX2_FIFO_DIR == `DIR_FX2_TO_PC) begin
			if ((MAX2_MODE == `MAX2_MODE_START) && (AUDIO_CAPTURE_ENABLED == 1)) begin
				// switch to next buffer
				if (audio_buffer_index == `AUDIO_CAPTURE_BUFFER_COUNT)
					audio_buffer_index <= 0;
				else
					audio_buffer_index <= audio_buffer_index + 1'b1;
			end
		end
	end
end

always @(posedge WSC_BCLK)
begin
	if (MAX2_FIFO_DIR == `DIR_FX2_TO_PC) begin		
		if ((MAX2_MODE == `MAX2_MODE_START) && (AUDIO_CAPTURE_ENABLED == 1)) begin						
			if (WSC_LRCK == 1) begin
				// left channel
				audio_capture_buffer[audio_buffer_index][15:0] <= audio_capture_buffer[audio_buffer_index][15:0] << 1;
				audio_capture_buffer[audio_buffer_index][0] <= WSC_SDAT;				
			end
			else begin
				// right channel
				audio_capture_buffer[audio_buffer_index][31:16] <= audio_capture_buffer[audio_buffer_index][31:16] << 1;
				audio_capture_buffer[audio_buffer_index][16] <= WSC_SDAT;								
			end
		end
	end
end
*/

// capture audio data (right justified data format)
always @(posedge WSC_LRCK or posedge CAPTURE_RESET)
begin
	if (CAPTURE_RESET == 1) begin
		first_audio_frame_arrived <= 1'b0;
	end
	else begin
		first_audio_frame_arrived <= 1'b1;
	end
end

always @(posedge WSC_BCLK or posedge CAPTURE_RESET)
begin
	if (CAPTURE_RESET == 1) begin
		// only reset index when MAX2 is reset
		// so the transfer index will keep counting in order
		if (MAX2_RESET == 1) begin
			audio_buffer_index <= 0;
			
			if (WSC_nRESET == 1)
				audio_buffer_bit_index <= 5'b11111;
			else
				audio_buffer_bit_index <= 0;
		end
		else
			audio_buffer_bit_index <= 5'b11111;
	end	
	else begin
		if (MAX2_FIFO_DIR == `DIR_FX2_TO_PC) begin
			if ((MAX2_MODE == `MAX2_MODE_START) && (AUDIO_CAPTURE_ENABLED == 1)) begin		
				if (WSC_LRCK == 1) begin
					// left channel
					audio_capture_buffer[audio_buffer_index][15:0] <= audio_capture_buffer[audio_buffer_index][15:0] << 1;
					audio_capture_buffer[audio_buffer_index][0] <= WSC_SDAT;
				end
				else begin
					// right channel
					audio_capture_buffer[audio_buffer_index][31:16] <= audio_capture_buffer[audio_buffer_index][31:16] << 1;
					audio_capture_buffer[audio_buffer_index][16] <= WSC_SDAT;				
				end

				// TODO use WSC_LRCK to switch buffer index so we don't need bit index
				begin
					if (audio_buffer_bit_index == 5'b11111) begin
						audio_buffer_bit_index <= 0;
						if (audio_buffer_index == `AUDIO_CAPTURE_BUFFER_COUNT)
							audio_buffer_index <= 0;
						else
							audio_buffer_index <= audio_buffer_index + 1'b1;					
					end
					else begin
						audio_buffer_bit_index <= audio_buffer_bit_index + 1'b1;
					end
				end
			end
		end	
	end
end

function [7:0] audio_fifo_write_data;
	input [1:0] byte_sel;
	input [31:0] data;
	begin
		// select which byte to transfer
		case (byte_sel)
			0: audio_fifo_write_data = data[7:0];
			1: audio_fifo_write_data = data[15:8];
			2: audio_fifo_write_data = data[23:16];
			3: audio_fifo_write_data = data[31:24];
		endcase
	end
endfunction

function [7:0] fifo_write_data;
	input byte_sel;
	input [15:0] data;
	begin
		if (byte_sel == 0)
			fifo_write_data = data[7:0];
		else
			fifo_write_data = data[15:8];
	end
endfunction

// collect read register data
function [7:0] read_reg_data;
	input [`MAX2_REG_ADDR_BITS - 1:0] reg_addr;
	input data_dir;
	
	begin
		if (data_dir == `DIR_FX2_TO_PC) begin
			case (reg_addr)
				// TODO
				default: read_reg_data = 0;
			endcase
		end
		else
			read_reg_data = 0;
	end
endfunction

endmodule
