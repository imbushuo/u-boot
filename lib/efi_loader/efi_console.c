/*
 *  EFI application console interface
 *
 *  Copyright (c) 2016 Alexander Graf
 *
 *  SPDX-License-Identifier:     GPL-2.0+
 */

#include <common.h>
#include <charset.h>
#include <dm/device.h>
#include <efi_loader.h>
#include <stdio_dev.h>
#include <video_console.h>

static bool console_size_queried;

#define EFI_COUT_MODE_2 2
#define EFI_MAX_COUT_MODE 3

struct cout_mode {
	unsigned long columns;
	unsigned long rows;
	int present;
};

static struct cout_mode efi_cout_modes[] = {
	/* EFI Mode 0 is 80x25 and always present */
	{
		.columns = 80,
		.rows = 25,
		.present = 1,
	},
	/* EFI Mode 1 is always 80x50 */
	{
		.columns = 80,
		.rows = 50,
		.present = 0,
	},
	/* Value are unknown until we query the console */
	{
		.columns = 0,
		.rows = 0,
		.present = 0,
	},
};

const efi_guid_t efi_guid_console_control = CONSOLE_CONTROL_GUID;

static struct stdio_dev *efiin, *efiout;

static int efi_tstc(void)
{
	return efiin->tstc(efiin);
}

static int efi_getc(void)
{
	return efiin->getc(efiin);
}

static int efi_printf(const char *fmt, ...)
{
	va_list args;
	uint i;
	char printbuffer[CONFIG_SYS_PBSIZE];

	va_start(args, fmt);

	/*
	 * For this to work, printbuffer must be larger than
	 * anything we ever want to print.
	 */
	i = vsnprintf(printbuffer, sizeof(printbuffer), fmt, args);
	va_end(args);

	/* Print the string */
	efiout->puts(efiout, printbuffer);
	return i;
}

#define cESC '\x1b'
#define ESC "\x1b"

/*
 * EFI_CONSOLE_CONTROL:
 */

static efi_status_t EFIAPI efi_cin_get_mode(
			struct efi_console_control_protocol *this,
			int *mode, char *uga_exists, char *std_in_locked)
{
	EFI_ENTRY("%p, %p, %p, %p", this, mode, uga_exists, std_in_locked);

	if (mode)
		*mode = EFI_CONSOLE_MODE_TEXT;
	if (uga_exists)
		*uga_exists = 0;
	if (std_in_locked)
		*std_in_locked = 0;

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cin_set_mode(
			struct efi_console_control_protocol *this, int mode)
{
	EFI_ENTRY("%p, %d", this, mode);
	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI efi_cin_lock_std_in(
			struct efi_console_control_protocol *this,
			uint16_t *password)
{
	EFI_ENTRY("%p, %p", this, password);
	return EFI_EXIT(EFI_UNSUPPORTED);
}

const struct efi_console_control_protocol efi_console_control = {
	.get_mode = efi_cin_get_mode,
	.set_mode = efi_cin_set_mode,
	.lock_std_in = efi_cin_lock_std_in,
};

/* Default to mode 0 */
static struct simple_text_output_mode efi_con_mode = {
	.max_mode = 1,
	.mode = 0,
	.attribute = 0,
	.cursor_column = 0,
	.cursor_row = 0,
	.cursor_visible = 1,
};


/*
 * EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL:
 */

static int term_read_reply(int *n, int maxnum, char end_char)
{
	char c;
	int i = 0;

	c = efi_getc();
	if (c != cESC)
		return -1;
	c = efi_getc();
	if (c != '[')
		return -1;

	n[0] = 0;
	while (1) {
		c = efi_getc();
		if (c == ';') {
			i++;
			if (i >= maxnum)
				return -1;
			n[i] = 0;
			continue;
		} else if (c == end_char) {
			break;
		} else if (c > '9' || c < '0') {
			return -1;
		}

		/* Read one more decimal position */
		n[i] *= 10;
		n[i] += c - '0';
	}

	return 0;
}

static efi_status_t EFIAPI efi_cout_reset(
			struct efi_simple_text_output_protocol *this,
			char extended_verification)
{
	EFI_ENTRY("%p, %d", this, extended_verification);
	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI efi_cout_output_string(
			struct efi_simple_text_output_protocol *this,
			const efi_string_t string)
{
	struct simple_text_output_mode *con = &efi_con_mode;
	struct cout_mode *mode = &efi_cout_modes[con->mode];

	EFI_ENTRY("%p, %p", this, string);

	unsigned int n16 = utf16_strlen(string);
	char buf[MAX_UTF8_PER_UTF16 * n16 + 1];
	char *p;

	*utf16_to_utf8((u8 *)buf, string, n16) = '\0';

	efiout->puts(efiout, buf);

	for (p = buf; *p; p++) {
		switch (*p) {
		case '\r':   /* carriage-return */
			con->cursor_column = 0;
			break;
		case '\n':   /* newline */
			con->cursor_column = 0;
			con->cursor_row++;
			break;
		case '\t':   /* tab, assume 8 char align */
			break;
		case '\b':   /* backspace */
			con->cursor_column = max(0, con->cursor_column - 1);
			break;
		default:
			con->cursor_column++;
			break;
		}
		if (con->cursor_column >= mode->columns) {
			con->cursor_column = 0;
			con->cursor_row++;
		}
		con->cursor_row = min(con->cursor_row, (s32)mode->rows - 1);
	}

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cout_test_string(
			struct efi_simple_text_output_protocol *this,
			const efi_string_t string)
{
	EFI_ENTRY("%p, %p", this, string);
	return EFI_EXIT(EFI_SUCCESS);
}

static bool cout_mode_matches(struct cout_mode *mode, int rows, int cols)
{
	if (!mode->present)
		return false;

	return (mode->rows == rows) && (mode->columns == cols);
}

static int query_console_serial(int *rows, int *cols)
{
	/* Ask the terminal about its size */
	int n[3];
	u64 timeout;

	/* Empty input buffer */
	while (efi_tstc())
		efi_getc();

	efi_printf(ESC"[18t");

	/* Check if we have a terminal that understands */
	timeout = timer_get_us() + 1000000;
	while (!efi_tstc())
		if (timer_get_us() > timeout)
			return -1;

	/* Read {depth,rows,cols} */
	if (term_read_reply(n, 3, 't'))
		return -1;

	*cols = n[2];
	*rows = n[1];

	return 0;
}

static efi_status_t EFIAPI efi_cout_query_mode(
			struct efi_simple_text_output_protocol *this,
			unsigned long mode_number, unsigned long *columns,
			unsigned long *rows)
{
	EFI_ENTRY("%p, %ld, %p, %p", this, mode_number, columns, rows);

	if (!console_size_queried) {
		int rows, cols;

		console_size_queried = true;

		if (!strcmp(efiout->name, "vidconsole") &&
		    IS_ENABLED(CONFIG_DM_VIDEO)) {
			struct udevice *dev = efiout->priv;
			struct vidconsole_priv *priv =
				dev_get_uclass_priv(dev);
			rows = priv->rows;
			cols = priv->cols;
		} else if (query_console_serial(&rows, &cols)) {
			goto out;
		}

		/* Test if we can have Mode 1 */
		if (cols >= 80 && rows >= 50) {
			efi_cout_modes[1].present = 1;
			efi_con_mode.max_mode = 2;
		}

		/*
		 * Install our mode as mode 2 if it is different
		 * than mode 0 or 1 and set it  as the currently selected mode
		 */
		if (!cout_mode_matches(&efi_cout_modes[0], rows, cols) &&
		    !cout_mode_matches(&efi_cout_modes[1], rows, cols)) {
			efi_cout_modes[EFI_COUT_MODE_2].columns = cols;
			efi_cout_modes[EFI_COUT_MODE_2].rows = rows;
			efi_cout_modes[EFI_COUT_MODE_2].present = 1;
			efi_con_mode.max_mode = EFI_MAX_COUT_MODE;
			efi_con_mode.mode = EFI_COUT_MODE_2;
		}
	}

	if (mode_number >= efi_con_mode.max_mode)
		return EFI_EXIT(EFI_UNSUPPORTED);

	if (efi_cout_modes[mode_number].present != 1)
		return EFI_EXIT(EFI_UNSUPPORTED);

out:
	if (columns)
		*columns = efi_cout_modes[mode_number].columns;
	if (rows)
		*rows = efi_cout_modes[mode_number].rows;

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cout_set_mode(
			struct efi_simple_text_output_protocol *this,
			unsigned long mode_number)
{
	EFI_ENTRY("%p, %ld", this, mode_number);


	if (mode_number > efi_con_mode.max_mode)
		return EFI_EXIT(EFI_UNSUPPORTED);

	efi_con_mode.mode = mode_number;
	efi_con_mode.cursor_column = 0;
	efi_con_mode.cursor_row = 0;

	return EFI_EXIT(EFI_SUCCESS);
}

static const struct {
	unsigned int fg;
	unsigned int bg;
} color[] = {
	{ 30, 40 },     /* 0: black */
	{ 34, 44 },     /* 1: blue */
	{ 32, 42 },     /* 2: green */
	{ 36, 46 },     /* 3: cyan */
	{ 31, 41 },     /* 4: red */
	{ 35, 45 },     /* 5: magenta */
	{ 33, 43 },     /* 6: brown, map to yellow as edk2 does*/
	{ 37, 47 },     /* 7: light grey, map to white */
};

/* See EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.SetAttribute(). */
static efi_status_t EFIAPI efi_cout_set_attribute(
			struct efi_simple_text_output_protocol *this,
			unsigned long attribute)
{
	unsigned int bold = EFI_ATTR_BOLD(attribute);
	unsigned int fg = EFI_ATTR_FG(attribute);
	unsigned int bg = EFI_ATTR_BG(attribute);

	EFI_ENTRY("%p, %lx", this, attribute);

	if (attribute)
		efi_printf(ESC"[%u;%u;%um", bold, color[fg].fg, color[bg].bg);
	else
		efi_printf(ESC"[0;37;40m");

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cout_clear_screen(
			struct efi_simple_text_output_protocol *this)
{
	EFI_ENTRY("%p", this);

	efi_printf(ESC"[2J");

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cout_set_cursor_position(
			struct efi_simple_text_output_protocol *this,
			unsigned long column, unsigned long row)
{
	EFI_ENTRY("%p, %ld, %ld", this, column, row);

	efi_printf(ESC"[%d;%df", (int)row, (int)column);
	efi_con_mode.cursor_column = column;
	efi_con_mode.cursor_row = row;

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cout_enable_cursor(
			struct efi_simple_text_output_protocol *this,
			bool enable)
{
	EFI_ENTRY("%p, %d", this, enable);

	efi_printf(ESC"[?25%c", enable ? 'h' : 'l');

	return EFI_EXIT(EFI_SUCCESS);
}

const struct efi_simple_text_output_protocol efi_con_out = {
	.reset = efi_cout_reset,
	.output_string = efi_cout_output_string,
	.test_string = efi_cout_test_string,
	.query_mode = efi_cout_query_mode,
	.set_mode = efi_cout_set_mode,
	.set_attribute = efi_cout_set_attribute,
	.clear_screen = efi_cout_clear_screen,
	.set_cursor_position = efi_cout_set_cursor_position,
	.enable_cursor = efi_cout_enable_cursor,
	.mode = (void*)&efi_con_mode,
};


/*
 * EFI_SIMPLE_TEXT_INPUT_PROTOCOL:
 */

/*
 * FIFO to buffer up key-strokes, to allow dispatching key event
 * notifications in advance of someone calling ReadKeyStroke().
 */

struct key_fifo {
	unsigned rd, wr;
	struct efi_key_data key[32]; /* use PoT size */
};

/* number of item's queued in fifo: */
static unsigned fifo_count(struct key_fifo *fifo)
{
	return (ARRAY_SIZE(fifo->key) + fifo->wr - fifo->rd) % ARRAY_SIZE(fifo->key);
}

/* remaining space to queue items in fifo: */
static unsigned fifo_space(struct key_fifo *fifo)
{
	return ARRAY_SIZE(fifo->key) - 1 - fifo_count(fifo);
}

/* push an item onto the tail of the fifo: */
static void fifo_push(struct key_fifo *fifo, struct efi_key_data *key)
{
	assert(fifo_space(fifo) >= 1);
	fifo->key[fifo->wr] = *key;
	fifo->wr = (fifo->wr + 1) % ARRAY_SIZE(fifo->key);
}

/* pop an item from the head of the fifo: */
static void fifo_pop(struct key_fifo *fifo, struct efi_key_data *key)
{
	assert(fifo_count(fifo) >= 1);
	*key = fifo->key[fifo->rd];
	fifo->rd = (fifo->rd + 1) % ARRAY_SIZE(fifo->key);
}

static struct key_fifo fifo;

static void notify_key(struct efi_key_data *key);

static efi_status_t EFIAPI efi_cin_reset(
			struct efi_simple_input_interface *this,
			bool extended_verification)
{
	EFI_ENTRY("%p, %d", this, extended_verification);
	fifo.rd = fifo.wr = 0;
	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t read_key_stroke(struct efi_key_data *key_data)
{
	struct efi_input_key pressed_key = {
		.scan_code = 0,
		.unicode_char = 0,
	};
	struct efi_key_state key_state = {
		.key_shift_state = 0,
		.key_toggle_state = 0,
	};
	char ch;

	if (!efi_tstc()) {
		/* No key pressed */
		return EFI_NOT_READY;
	}

	ch = efi_getc();
	if (ch == cESC) {
		/* Escape Sequence */
		ch = efi_getc();
		switch (ch) {
		case cESC: /* ESC */
			pressed_key.scan_code = 23;
			break;
		case 'O': /* F1 - F4 */
			pressed_key.scan_code = efi_getc() - 'P' + 11;
			break;
		case 'a'...'z':
			key_state.key_shift_state =
				EFI_SHIFT_STATE_VALID | EFI_EFI_LEFT_ALT_PRESSED;
			break;
		case '[':
			ch = efi_getc();
			switch (ch) {
			case 'A'...'D': /* up, down right, left */
				pressed_key.scan_code = ch - 'A' + 1;
				break;
			case 'F': /* End */
				pressed_key.scan_code = 6;
				break;
			case 'H': /* Home */
				pressed_key.scan_code = 5;
				break;
			case '1': /* F5 - F8 */
				pressed_key.scan_code = efi_getc() - '0' + 11;
				efi_getc();
				break;
			case '2': /* F9 - F12 */
				pressed_key.scan_code = efi_getc() - '0' + 19;
				efi_getc();
				break;
			case '3': /* DEL */
				pressed_key.scan_code = 8;
				efi_getc();
				break;
			}
			break;
		}
	} else if (0x01 <= ch && ch <= 0x1a && ch != '\t' && ch != '\b' &&
		   ch != '\n' && ch != '\r') {
		/*
		 * Ctrl + <letter>.. except for a few cases that conflict
		 * with unmodified chars
		 */
		ch = ch + 'a' - 1;
		key_state.key_shift_state =
			EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED;
	} else if (ch == 0x7f) {
		/* Backspace */
		ch = 0x08;
	}
	pressed_key.unicode_char = ch;
	key_data->key = pressed_key;
	key_data->key_state = key_state;

	return EFI_SUCCESS;
}

static void read_keys(void)
{
	struct efi_key_data key;

	while (fifo_space(&fifo) > 0 && read_key_stroke(&key) == EFI_SUCCESS) {
		notify_key(&key);
		fifo_push(&fifo, &key);
	}
}

static efi_status_t EFIAPI efi_cin_read_key_stroke(
			struct efi_simple_input_interface *this,
			struct efi_input_key *key)
{
	struct efi_key_data key_data;

	EFI_ENTRY("%p, %p", this, key);

	while (true) {
		efi_timer_check();
		read_keys();

		if (fifo_count(&fifo) == 0)
			return EFI_EXIT(EFI_NOT_READY);

		fifo_pop(&fifo, &key_data);

		/* ignore ctrl/alt/etc */
		if (key_data.key_state.key_shift_state)
			continue;

		*key = key_data.key;

		return EFI_EXIT(EFI_SUCCESS);
	}
}

struct efi_simple_input_interface efi_con_in = {
	.reset = efi_cin_reset,
	.read_key_stroke = efi_cin_read_key_stroke,
	.wait_for_key = NULL,
};

static struct efi_event *console_timer_event;

static void EFIAPI efi_key_notify(struct efi_event *event, void *context)
{
}

static void EFIAPI efi_console_timer_notify(struct efi_event *event,
					    void *context)
{
	EFI_ENTRY("%p, %p", event, context);
	if (efi_tstc()) {
		read_keys();
		efi_con_in.wait_for_key->is_signaled = true;
		efi_signal_event(efi_con_in.wait_for_key);
	}
	EFI_EXIT(EFI_SUCCESS);
}


/*
 * EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL
 */

struct key_notifier {
	struct list_head link;
	struct efi_key_data key;
	efi_status_t (EFIAPI *notify)(struct efi_key_data *key);
};

static LIST_HEAD(key_notifiers);  /* list of key_notifier */

static bool match_key(struct efi_key_data *a, struct efi_key_data *b)
{
	return (a->key.scan_code == b->key.scan_code) &&
	       (a->key.unicode_char == b->key.unicode_char) &&
	       (a->key_state.key_shift_state == b->key_state.key_shift_state) &&
	       (a->key_state.key_toggle_state == b->key_state.key_toggle_state);
}

static void notify_key(struct efi_key_data *key)
{
	struct key_notifier *notifier;

	list_for_each_entry(notifier, &key_notifiers, link)
		if (match_key(&notifier->key, key))
			EFI_CALL(notifier->notify(key));
}

static efi_status_t EFIAPI efi_cin_ex_reset(
		struct efi_simple_text_input_ex_interface *this,
		bool extended_verification)
{
	EFI_ENTRY("%p, %d", this, extended_verification);
	fifo.rd = fifo.wr = 0;
	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI efi_cin_ex_read_key_stroke(
		struct efi_simple_text_input_ex_interface *this,
		struct efi_key_data *key_data)
{
	EFI_ENTRY("%p, %p", this, key_data);

	/* We don't do interrupts, so check for timers cooperatively */
	efi_timer_check();
	read_keys();

	if (fifo_count(&fifo) == 0)
		return EFI_EXIT(EFI_NOT_READY);

	fifo_pop(&fifo, key_data);

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cin_ex_set_state(
		struct efi_simple_text_input_ex_interface *this,
		uint8_t key_toggle_state)
{
	EFI_ENTRY("%p, %x", this, key_toggle_state);
	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cin_ex_register_key_notify(
		struct efi_simple_text_input_ex_interface *this,
		struct efi_key_data *key_data,
		efi_status_t (EFIAPI *notify_fn)(struct efi_key_data *key_data),
		efi_handle_t *notify_handle)
{
	struct key_notifier *notifier;

	EFI_ENTRY("%p, %p, %p", this, notify_fn, notify_handle);
	notifier = calloc(1, sizeof(*notifier));
	if (!notifier)
	{
		#ifdef CONFIG_EFI_TRACING_X
			printf("EFI Tracing: %s:%d reported EFI_OUT_OF_RESOURCES\n", __func__, __LINE__);
		#endif
		return EFI_EXIT(EFI_OUT_OF_RESOURCES);
	}
		
	notifier->notify = notify_fn;
	notifier->key = *key_data;

	list_add_tail(&notifier->link, &key_notifiers);

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI efi_cin_ex_unregister_key_notify(
		struct efi_simple_text_input_ex_interface *this,
		efi_handle_t notify_handle)
{
	struct key_notifier *notifier = notify_handle;

	EFI_ENTRY("%p, %p", this, notify_handle);

	list_del(&notifier->link);
	free(notifier);

	return EFI_EXIT(EFI_SUCCESS);
}

static struct efi_simple_text_input_ex_interface efi_con_in_ex = {
	.reset = efi_cin_ex_reset,
	.read_key_stroke = efi_cin_ex_read_key_stroke,
	.wait_for_key = NULL,
	.set_state = efi_cin_ex_set_state,
	.register_key_notify = efi_cin_ex_register_key_notify,
	.unregister_key_notify = efi_cin_ex_unregister_key_notify,
};

static struct efi_object efi_console_control_obj = {
	.protocols =  {
		{ &efi_guid_console_control, (void *)&efi_console_control },
	},
	.handle = &efi_console_control_obj,
};

struct efi_object efi_console_output_obj = {
	.protocols = {
		{&EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID, (void *)&efi_con_out},
	},
	.handle = &efi_console_output_obj,
};

struct efi_object efi_console_input_obj = {
	.protocols = {
		{&EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,    (void *)&efi_con_in},
		{&EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID, (void *)&efi_con_in_ex},
	},
	.handle = &efi_console_input_obj,
};

static struct stdio_dev *get_stdio_dev(const char *envname, int default_dev)
{
	const char *name;
	struct stdio_dev *dev = NULL;

	name = env_get(envname);
	if (name) {
		dev = stdio_get_by_name(name);
		if (dev && dev->start) {
			int ret = dev->start(dev);
			if (ret < 0)
				dev = NULL;
		}
	}

	if (!dev)
		dev = stdio_devices[default_dev];

	return dev;
}

/* This gets called from do_bootefi_exec(). */
int efi_console_register(void)
{
	efi_status_t r;

	/* Hook up to the device list */
	list_add_tail(&efi_console_control_obj.link, &efi_obj_list);
	list_add_tail(&efi_console_output_obj.link, &efi_obj_list);
	list_add_tail(&efi_console_input_obj.link, &efi_obj_list);

	efiout = get_stdio_dev("efiout", stdout);
	efiin  = get_stdio_dev("efiin",  stdin);

	r = efi_create_event(EVT_NOTIFY_WAIT, TPL_CALLBACK,
			     efi_key_notify, NULL, &efi_con_in.wait_for_key);
	if (r != EFI_SUCCESS) {
		printf("ERROR: Failed to register WaitForKey event\n");
		return r;
	}
	r = efi_create_event(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
			     efi_console_timer_notify, NULL,
			     &console_timer_event);
	if (r != EFI_SUCCESS) {
		printf("ERROR: Failed to register console event\n");
		return r;
	}
	/* 5000 ns cycle is sufficient for 2 MBaud */
	r = efi_set_timer(console_timer_event, EFI_TIMER_PERIODIC, 50);
	if (r != EFI_SUCCESS)
		printf("ERROR: Failed to set console timer\n");
	return r;
}
