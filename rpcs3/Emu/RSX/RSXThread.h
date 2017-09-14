#pragma once

#include <stack>
#include <deque>
#include <set>
#include <mutex>
#include <atomic>
#include "GCM.h"
#include "rsx_cache.h"
#include "RSXTexture.h"
#include "RSXVertexProgram.h"
#include "RSXFragmentProgram.h"
#include "rsx_methods.h"
#include "rsx_trace.h"
#include <Utilities/GSL.h>

#include "Utilities/Thread.h"
#include "Utilities/Timer.h"
#include "Utilities/geometry.h"
#include "rsx_trace.h"
#include "restore_new.h"
#include "Utilities/variant.hpp"
#include "define_new_memleakdetect.h"

#include "Emu/Cell/lv2/sys_rsx.h"

extern u64 get_system_time();

extern bool user_asked_for_frame_capture;
extern rsx::frame_capture_data frame_debug;

namespace rsx
{
	namespace limits
	{
		enum
		{
			fragment_textures_count = 16,
			vertex_textures_count = 4,
			vertex_count = 16,
			fragment_count = 32,
			tiles_count = 15,
			zculls_count = 8,
			color_buffers_count = 4
		};
	}

	u32 get_vertex_type_size_on_host(vertex_base_type type, u32 size);

	u32 get_address(u32 offset, u32 location);

	struct tiled_region
	{
		u32 address;
		u32 base;
		GcmTileInfo *tile;
		u8 *ptr;

		void write(const void *src, u32 width, u32 height, u32 pitch);
		void read(void *dst, u32 width, u32 height, u32 pitch);
	};

	struct vertex_array_buffer
	{
		rsx::vertex_base_type type;
		u8 attribute_size;
		u8 stride;
		gsl::span<const gsl::byte> data;
		u8 index;
	};

	struct vertex_array_register
	{
		rsx::vertex_base_type type;
		u8 attribute_size;
		std::array<u32, 4> data;
		u8 index;
	};

	struct empty_vertex_array
	{
		u8 index;
	};

	struct draw_array_command
	{
		/**
		* First and count of index subranges.
		*/
		std::vector<std::pair<u32, u32>> indexes_range;
	};

	struct draw_indexed_array_command
	{
		/**
		* First and count of subranges to fetch in index buffer.
		*/
		std::vector<std::pair<u32, u32>> ranges_to_fetch_in_index_buffer;

		gsl::span<const gsl::byte> raw_index_buffer;
	};

	struct draw_inlined_array
	{
		std::vector<u32> inline_vertex_array;
	};

	struct interleaved_range_info
	{
		bool interleaved = false;
		bool all_modulus = false;
		bool single_vertex = false;
		u32  base_offset = 0;
		u32  real_offset_address = 0;
		u8   memory_location = 0;
		u8   attribute_stride = 0;
		u16  min_divisor = 0;

		std::vector<u8> locations;
	};

	enum attribute_buffer_placement : u8
	{
		none = 0,
		persistent = 1,
		transient = 2
	};

	struct vertex_input_layout
	{
		std::vector<interleaved_range_info> interleaved_blocks;  //Interleaved blocks to be uploaded as-is
		std::vector<std::pair<u8, u32>> volatile_blocks;  //Volatile data blocks (immediate draw vertex data for example)
		std::vector<u8> referenced_registers;  //Volatile register data

		std::array<attribute_buffer_placement, 16> attribute_placement;
	};

	class thread : public named_thread
	{
		std::shared_ptr<thread_ctrl> m_vblank_thread;

	protected:
		std::stack<u32> m_call_stack;
		std::array<push_buffer_vertex_info, 16> vertex_push_buffers;
		std::vector<u32> element_push_buffer;

		s32 m_skip_frame_ctr = 0;
		bool skip_frame = false;

		bool supports_multidraw = false;

	public:
		RsxDmaControl* ctrl = nullptr;
		atomic_t<u32> internal_get{ 0 };

		Timer timer_sync;

		GcmTileInfo tiles[limits::tiles_count];
		GcmZcullInfo zculls[limits::zculls_count];

		// Constant stored for whole frame
		std::unordered_map<u32, color4f> local_transform_constants;

		bool capture_current_frame = false;
		void capture_frame(const std::string &name);

	public:
		std::shared_ptr<class ppu_thread> intr_thread;

		// I hate this flag, but until hle is closer to lle, its needed
		bool isHLE{ false };

		u32 ioAddress, ioSize;
		u32 flip_status;
		int debug_level;

		atomic_t<bool> requested_vsync{false};
		atomic_t<bool> enable_second_vhandler{false};

		RsxDisplayInfo display_buffers[8];
		u32 display_buffers_count{0};
		u32 current_display_buffer{0};
		u32 ctxt_addr;
		u32 label_addr;

		u32 local_mem_addr, main_mem_addr;

		bool m_rtts_dirty;
		bool m_transform_constants_dirty;
		bool m_textures_dirty[16];

	protected:
		std::array<u32, 4> get_color_surface_addresses() const;
		u32 get_zeta_surface_address() const;

		/**
		 * Analyze vertex inputs and group all interleaved blocks
		 */
		vertex_input_layout analyse_inputs_interleaved() const;

		RSXVertexProgram current_vertex_program = {};
		RSXFragmentProgram current_fragment_program = {};

		void get_current_vertex_program();

		/**
		 * Gets current fragment program and associated fragment state
		 * get_surface_info is a helper takes 2 parameters: rsx_texture_address and surface_is_depth
		 * returns whether surface is a render target and surface pitch in native format
		 */
		void get_current_fragment_program(std::function<std::tuple<bool, u16>(u32, fragment_texture&, bool)> get_surface_info);
	public:
		double fps_limit = 59.94;

	public:
		u64 last_flip_time;
		vm::ps3::ptr<void(u32)> flip_handler = vm::null;
		vm::ps3::ptr<void(u32)> user_handler = vm::null;
		vm::ps3::ptr<void(u32)> vblank_handler = vm::null;
		u64 vblank_count;

	public:
		std::set<u32> m_used_gcm_commands;
		bool invalid_command_interrupt_raised = false;
		bool in_begin_end = false;

		bool conditional_render_test_failed = false;
		bool conditional_render_enabled = false;
		bool zcull_stats_enabled = false;
		bool zcull_rendering_enabled = false;
		bool zcull_pixel_cnt_enabled = false;

	protected:
		thread();
		virtual ~thread();

		virtual void on_task() override;
		virtual void on_exit() override;
		
		/**
		 * Execute a backend local task queue
		 */
		virtual void do_local_task() {}

	public:
		virtual std::string get_name() const override;

		virtual void on_init(const std::shared_ptr<void>&) override {} // disable start() (TODO)
		virtual void on_stop() override {} // disable join()

		virtual void begin();
		virtual void end();

		virtual void on_init_rsx() = 0;
		virtual void on_init_thread() = 0;
		virtual bool do_method(u32 /*cmd*/, u32 /*value*/) { return false; }
		virtual void flip(int buffer) = 0;
		virtual u64 timestamp() const;
		virtual bool on_access_violation(u32 /*address*/, bool /*is_writing*/) { return false; }
		virtual void on_notify_memory_unmapped(u32 /*address_base*/, u32 /*size*/) {}

		//zcull
		virtual void notify_zcull_info_changed() {}
		virtual void clear_zcull_stats(u32 /*type*/) {}
		virtual u32 get_zcull_stats(u32 /*type*/) { return UINT16_MAX; }

		gsl::span<const gsl::byte> get_raw_index_array(const std::vector<std::pair<u32, u32> >& draw_indexed_clause) const;
		gsl::span<const gsl::byte> get_raw_vertex_buffer(const rsx::data_array_format_info&, u32 base_offset, const std::vector<std::pair<u32, u32>>& vertex_ranges) const;

		std::vector<std::variant<vertex_array_buffer, vertex_array_register, empty_vertex_array>>
		get_vertex_buffers(const rsx::rsx_state& state, const std::vector<std::pair<u32, u32>>& vertex_ranges, const u64 consumed_attrib_mask) const;
		
		std::variant<draw_array_command, draw_indexed_array_command, draw_inlined_array>
		get_draw_command(const rsx::rsx_state& state) const;

		/**
		* Immediate mode rendering requires a temp push buffer to hold attrib values
		* Appends a value to the push buffer (currently only supports 32-wide types)
		*/
		void append_to_push_buffer(u32 attribute, u32 size, u32 subreg_index, vertex_base_type type, u32 value);
		u32 get_push_buffer_vertex_count() const;

		void append_array_element(u32 index);
		u32 get_push_buffer_index_count() const;

	protected:

		/**
		 * Computes VRAM requirements needed to upload raw vertex streams
		 * result.first contains persistent memory requirements
		 * result.second contains volatile memory requirements
		 */
		std::pair<u32, u32> calculate_memory_requirements(vertex_input_layout& layout, const u32 vertex_count);

		/**
		 * Generates vertex input descriptors as an array of 16x4 s32s
		 */
		void fill_vertex_layout_state(vertex_input_layout& layout, const u32 vertex_count, s32* buffer);

		/**
		 * Uploads vertex data described in the layout descriptor
		 * Copies from local memory to the write-only output buffers provided in a sequential manner
		 */
		void write_vertex_data_to_memory(vertex_input_layout &layout, const u32 first_vertex, const u32 vertex_count, void *persistent_data, void *volatile_data);

	private:
		std::mutex m_mtx_task;

		struct internal_task_entry
		{
			std::function<bool()> callback;
			//std::promise<void> promise;

			internal_task_entry(std::function<bool()> callback) : callback(callback)
			{
			}
		};

		std::deque<internal_task_entry> m_internal_tasks;
		void do_internal_task();

	public:
		//std::future<void> add_internal_task(std::function<bool()> callback);
		//void invoke(std::function<bool()> callback);

		/**
		 * Fill buffer with 4x4 scale offset matrix.
		 * Vertex shader's position is to be multiplied by this matrix.
		 * if flip_y is set, the matrix is modified to use d3d convention.
		 */
		void fill_scale_offset_data(void *buffer, bool flip_y) const;

		/**
		 * Fill buffer with user clip information
		*/

		void fill_user_clip_data(void *buffer) const;

		/**
		* Fill buffer with vertex program constants.
		* Buffer must be at least 512 float4 wide.
		*/
		void fill_vertex_program_constants_data(void *buffer);

		/**
		 * Fill buffer with fragment rasterization state.
		 * Fills current fog values, alpha test parameters and texture scaling parameters
		 */
		void fill_fragment_state_buffer(void *buffer, const RSXFragmentProgram &fragment_program);

		/**
		* Write inlined array data to buffer.
		* The storage of inlined data looks different from memory stored arrays.
		* There is no swapping required except for 4 u8 (according to Bleach Soul Resurection)
		*/
		void write_inline_array_to_buffer(void *dst_buffer);

		/**
		 * Copy rtt values to buffer.
		 * TODO: It's more efficient to combine multiple call of this function into one.
		 */
		virtual std::array<std::vector<gsl::byte>, 4> copy_render_targets_to_memory() {
			return  std::array<std::vector<gsl::byte>, 4>();
		};

		/**
		* Copy depth and stencil content to buffers.
		* TODO: It's more efficient to combine multiple call of this function into one.
		*/
		virtual std::array<std::vector<gsl::byte>, 2> copy_depth_stencil_buffer_to_memory() {
			return std::array<std::vector<gsl::byte>, 2>();
		};

		virtual std::pair<std::string, std::string> get_programs() const { return std::make_pair("", ""); };

		virtual bool scaled_image_from_memory(blit_src_info& /*src_info*/, blit_dst_info& /*dst_info*/, bool /*interpolate*/){ return false;  }

	public:
		void reset();
		void init(u32 ioAddress, u32 ioSize, u32 ctrlAddress, u32 localAddress);

		tiled_region get_tiled_address(u32 offset, u32 location);
		GcmTileInfo *find_tile(u32 offset, u32 location);

		u32 ReadIO32(u32 addr);
		void WriteIO32(u32 addr, u32 value);
	};
}
