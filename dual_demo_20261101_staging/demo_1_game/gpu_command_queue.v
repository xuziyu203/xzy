module gpu_command_queue #(
    parameter integer DATA_WIDTH  = 64,
    parameter integer DEPTH       = 8,
    parameter integer ADDR_WIDTH  = (DEPTH <= 1) ? 1 : $clog2(DEPTH),
    parameter integer LEVEL_WIDTH = (DEPTH <= 1) ? 1 : $clog2(DEPTH + 1)
)
(
    input  wire                   clk,
    input  wire                   rst_n,

    input  wire                   push,
    input  wire [DATA_WIDTH-1:0]  push_data,

    input  wire                   pop,

    output wire [DATA_WIDTH-1:0]  head_data,
    output wire                   empty,
    output wire                   full,
    output wire [LEVEL_WIDTH-1:0] level
);

    reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
    reg [ADDR_WIDTH-1:0] wr_ptr;
    reg [ADDR_WIDTH-1:0] rd_ptr;
    reg [LEVEL_WIDTH-1:0] level_r;

    localparam [ADDR_WIDTH-1:0] LAST_ADDR = DEPTH - 1;
    localparam [LEVEL_WIDTH-1:0] DEPTH_COUNT = DEPTH;

    wire push_accept;
    wire pop_accept;

    assign empty = (level_r == {LEVEL_WIDTH{1'b0}});
    assign full  = (level_r == DEPTH_COUNT);
    assign level = level_r;

    // Show-head semantics: the current entry is visible before pop advances
    // rd_ptr at the active clock edge.
    assign head_data = empty ? {DATA_WIDTH{1'b0}} : mem[rd_ptr];

    // Boundary behavior is intentionally conservative. At full, a push is
    // rejected even if pop is also asserted; at empty, a pop is rejected even
    // if push is also asserted. This prevents overflow and underflow without
    // relying on same-cycle bypass behavior.
    assign push_accept = push && !full;
    assign pop_accept  = pop  && !empty;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr  <= {ADDR_WIDTH{1'b0}};
            rd_ptr  <= {ADDR_WIDTH{1'b0}};
            level_r <= {LEVEL_WIDTH{1'b0}};
        end
        else begin
            if (push_accept) begin
                mem[wr_ptr] <= push_data;

                if (wr_ptr == LAST_ADDR)
                    wr_ptr <= {ADDR_WIDTH{1'b0}};
                else
                    wr_ptr <= wr_ptr + 1'b1;
            end

            if (pop_accept) begin
                if (rd_ptr == LAST_ADDR)
                    rd_ptr <= {ADDR_WIDTH{1'b0}};
                else
                    rd_ptr <= rd_ptr + 1'b1;
            end

            case ({push_accept, pop_accept})
                2'b10: level_r <= level_r + 1'b1;
                2'b01: level_r <= level_r - 1'b1;
                default: level_r <= level_r;
            endcase
        end
    end

endmodule
