/*
 * CWinQuiz.cpp
 *
 *  Created on: Dec 12, 2017
 *      Author: spider
 */

#include <mainapp/impl/ui/CWinQuiz.h>
#include "mainapp/interface/ncurses/ICursesApp.h"
#include "mainapp/interface/ncurses/curses_events.h"
#include "mainapp/interface/model/quiz_model.h"
#include "mainapp/impl/core/CKernel.h"
#include <form.h>
#include <stdlib.h>
#include <string.h>
#include <ctime>
#include <chrono>
#include <iostream>

using namespace std;

/*
 +----------+--------------------------------------------------+
 | 21:00    |  Quiz Name: Name                                 |
 +----------+  Quiz Code: Code                                 |
 |             Author: author                                  |
 |                                                             |
 | Code01 +----------------------------------------+           |
 |        +----------------------------------------+           |
 |                                                             |
 | Item02 +----------------------------------------+           |
 |        +----------------------------------------+           |
 |                                                             |
 | Item03 +----------------------------------------+           |
 |        +----------------------------------------+           |
 |                                  +-----------++-----------+ |
 |                                  |  Start    ||  CANCEL   | |
 |                                  +-----------++-----------+ |
 +-------------------------------------------------------------+
*/

#define STATE_INIT 1
#define STATE_SHOW_FORM 2
#define STATE_LOADING_QUIZ 2
#define STATE_USER_CHOICE 3
#define STATE_WAIT_FOR_START 4
#define STATE_DOING_QUIZ 5
#define STATE_CACULATE_QUIZ 6
#define STATE_CANCEL_QUIZ 7
#define STATE_DISPLAY_SCORE 8
#define STATE_DISPLAY_ERROR 9
#define STATE_QUIT 10
#define STATE_CONFIRM_FINISH_QUIZ 11

#define MSG_LOADED_MEMQUIZ 1
#define MSG_LOAD_FAILED_MEMQUIZ 2
#define MSG_ERROR 3			// General Error => Orthogonal region ? 
#define MSG_START_QUIZ 4	// Start; Cancel; Quit
#define MSG_CLOSE_FORM 5
#define MSG_CANCEL_QUIZ 6	// Cancel an ongoing quiz
#define MSG_FINISH_QUIZ 7  	
#define MSG_TIMEOUT_QUIZ 8	// Time-up
#define MSG_DEFAULT 9		// Default Step
#define MSG_CLICK_START_QUIZ 10
#define MSG_CLICK_CLOSE_QUIZ 11
#define MSG_CLICK_COMPLETE_QUIZ 12
#define MSG_CLICK_CONFIRM_Y 13
#define MSG_CLICK_CONFIRM_N 14
#define MSG_CLICK_CANCEL 	15
#define MSG_CLICK_RESTART 	16

#define MSG_TIMER 17
#define MSG_TIMEUP 18

//typedef int (*TransitionHandler)(void*);
//
//typedef struct StructTransitionRow
//{
//	int from_State_;
//	int toState;
//	int message;
//	TransitionHandler handler;
//
//	struct StructTransitionRow(int from, int to, int msg, TransitionHandler f)
//	{
//		from_State_ = from;
//		toState = to;
//		message = msg;
//		handler = f;
//	}
//} TransitionRow;

// typedef int (CWinQuiz::*EventFunction)(void*);
// typedef struct STRUCTTransitionRow
// {
// 	int from_State_;
// 	int toState;
// 	int message;
// 	EventFunction handler;

// 	STRUCTTransitionRow(int from, int to, int msg, EventFunction evt)
// 	{
// 		from_State_ = from;
// 		toState = to;
// 		message = msg;
// 		handler = evt;
// 	}
// } TransitionRow;
/*
	States and Events:
	--------------------
	init
		--(msg_show)--> (onLoadQuiz_)loading_quiz 
	loading_quiz
		--(loaded_memquiz){ initDoingQuiz( [ init_curses, draw_quizform ]) } --> user choice
		--(load_failed_memquiz) { [ uninit_curses, drawErrorForm ] } --> display_error
	user_choice
		--(click_start_quiz_)--{ onWaitStart([ drawWaitstart ]) } ---> wait_start
		--(click_close_quiz_) { onCloseQuiz[ uninit_curses ] }---> quit
	wait_start
		--(timer ([ drawWaitStart ]) )--> wait_start
		--(timeup_wait_start{[ erase_wait_start_ ]})--> doing_quiz
	doing_quiz
		--(timer{ drawStopwatch } )---> doing_quiz
		--(timeup_ { stopStopwatch, calc_score_ } )--> display_score
		--(click_cancel_{[ draw_quizform, erase_stop_watch_ ]})--> user_choice
		--(click_complete_quiz)---> confirm_finish_quiz
	confirm_finish_quiz
		--(click_Y { stopStopwatch, calc_score_ })--> display_score
		--(click_N { } )--> doing_quiz
	display_error
		--(click_close_quiz_ { uninit_curses } ) --> quit
	display_score
		--(click_restart)--> wait_start
		--(click_close_quiz_)--> quit
	quit
		--(show_form)--> loading_quiz
	<<any_state>>
		--(keyboard{ onKeyboard[] })--><<any_state>>
	<<any_state>>
		--(mouse{ onMouse[] })--><<any_state>>
 */

TransitionRow CWinQuiz::_rows[] = {
		TransitionRow(STATE_LOADING_QUIZ, 		STATE_USER_CHOICE, 		MSG_LOADED_MEMQUIZ, &			CWinQuiz::initDoingQuiz),
		TransitionRow(STATE_LOADING_QUIZ, 		STATE_DISPLAY_ERROR, 	MSG_LOAD_FAILED_MEMQUIZ, 		&CWinQuiz::drawErrorForm),
		TransitionRow(STATE_USER_CHOICE, 		STATE_WAIT_FOR_START,	MSG_CLICK_START_QUIZ, 			&CWinQuiz::onWaitStart),
		TransitionRow(STATE_USER_CHOICE, 		STATE_QUIT,				MSG_CLICK_CLOSE_QUIZ, 			&CWinQuiz::onCloseQuiz),
		TransitionRow(STATE_WAIT_FOR_START,		STATE_WAIT_FOR_START,			MSG_TIMER,						&CWinQuiz::drawWaitstart),
		TransitionRow(STATE_WAIT_FOR_START,		STATE_WAIT_FOR_START,			MSG_TIMEUP,						&CWinQuiz::erase_wait_start_),
		TransitionRow(STATE_DOING_QUIZ,			STATE_DOING_QUIZ,				MSG_TIMER,						&CWinQuiz::erase_wait_start_),
		TransitionRow(STATE_DOING_QUIZ,			STATE_DISPLAY_SCORE,			MSG_TIMEUP,						&CWinQuiz::finishDoQuiz_),
		TransitionRow(STATE_DOING_QUIZ,			STATE_USER_CHOICE,				MSG_CLICK_CANCEL,				&CWinQuiz::onClickCancel),
		TransitionRow(STATE_DOING_QUIZ,			STATE_CONFIRM_FINISH_QUIZ,		MSG_CLICK_COMPLETE_QUIZ,		&CWinQuiz::showConfirm),	// New

		TransitionRow(STATE_CONFIRM_FINISH_QUIZ,		STATE_DISPLAY_SCORE,		MSG_CLICK_CONFIRM_Y,		&CWinQuiz::finishDoQuiz_),
		TransitionRow(STATE_CONFIRM_FINISH_QUIZ,		STATE_DOING_QUIZ,			MSG_CLICK_CONFIRM_N,		&CWinQuiz::resumeDoQuiz_),	// New
		TransitionRow(STATE_DISPLAY_ERROR,				STATE_QUIT,					MSG_CLICK_CLOSE_QUIZ,		&CWinQuiz::uninit_curses),
		TransitionRow(STATE_DISPLAY_SCORE, 				STATE_WAIT_FOR_START,		MSG_CLICK_RESTART, 			&CWinQuiz::onRestartQuiz)	// New
		// TransitionRow(STATE_DISPLAY_SCORE, 				STATE_QUIT,					MSG_CLICK_CLOSE_QUIZ, 		&CWinQuiz::onRestartQuiz),	
};

CWinQuiz::CWinQuiz() {
	_sw_seconds = 0;
	_pWin = NULL;
	_pWinSb = NULL;
	_pForm_ = NULL;
	_pFormError = NULL;
	_pFormConfirm = NULL;

	_win_height = 25;
	_win_width_ = 80;
	_toppos = 2;
	_left = 2;

	std::chrono::time_point<std::chrono::system_clock> tnow;
	tnow = std::chrono::system_clock::now();

	KERNEL->db()->getMemStick(string("Twenty"), _memory);
	_modelt = KERNEL->memorytest_()->generateTest(_memory);	// Model for test

	_answer.answer1 = "Answer1";
	_answer.answer2 = "Answer2";
	_answer.answer3 = "Answer3";

	_innerstate = STATE_INIT;	// Start state machine
}

CWinQuiz::~CWinQuiz() {
}

void CWinQuiz::doModal1()
{
	FIELD* field[5];
	FORM* my_form;
	WINDOW* my_form_win;
	WINDOW* win_sub;
	int ch, rows, cols;
	MEVENT m_event;					// Mouse events

	/* Initialize curses */
	initscr();
	start_color();
	cbreak();
	noecho();
	// raw();
	keypad(stdscr, TRUE);

	/* Initialize few color paris */
	init_pair(1, COLOR_RED, COLOR_YELLOW);
	init_pair(2, COLOR_WHITE, COLOR_BLUE);

	/* Initialize the fields */
	field[0] = new_field(1, 20, 6, 1, 0, 3);
	field[1] = new_field(1, 10, 8, 1, 0, 0);
	field[2] = new_field(1, 10, 10, 1, 0, 0);
	field[3] = new_field(1, 10, 12, 1, 0, 0);
	field[4] = NULL;

	/* Set field options */
	set_field_back(field[0], A_UNDERLINE);
	field_opts_off(field[0], O_AUTOSKIP);	/* Don't go to next field when this */
											/* Field is filled up */
	set_field_back(field[1], A_UNDERLINE);
	field_opts_off(field[1], O_AUTOSKIP);
	set_field_pad(field[1], '@');
	set_field_fore(field[1], COLOR_PAIR(2));

	set_field_back(field[2], A_UNDERLINE);
	field_opts_off(field[2], O_AUTOSKIP);
	set_field_fore(field[2], COLOR_PAIR(2));

	set_field_back(field[3], A_UNDERLINE);
	field_opts_off(field[3], O_AUTOSKIP);
	set_field_fore(field[3], COLOR_PAIR(2));

	/* Create the form and post it */
	my_form = new_form(field);

	/* Create the window and sub window */
	scale_form(my_form, &rows, &cols);

	/* Create the Window to be associated with the form */
	my_form_win = newwin(rows + 4, cols + 4, 4, 4);
	keypad(my_form_win, TRUE);

	/* Set main window and sub window */
	win_sub = derwin(my_form_win, rows, cols, 2, 2);
	set_form_win(my_form, my_form_win);
	set_form_sub(my_form, win_sub);

	/* Print a border around the main Window and print a title */
	
	// box(my_form_win, 0, 0);
	wborder(my_form_win, '|', '|', '-', '-', '+', '+', '+', '+');
	// wborder(my_form_win, '|', '║', '-', '╧', '╗', '╚','╔','╝');
	// wborder(my_form_win, '|', ':', '-', '_', '+', '&','*','@');
	Util::print_in_middle(my_form_win, 1, 0, cols + 4, "My Form", COLOR_PAIR(1));

	// wrefresh(my_form_win);
	post_form(my_form);

	mvprintw(LINES - 2, 0, "Use UP, DOWN arrow keys to switch between fields");
	refresh();

	while ((ch = wgetch(my_form_win)) != KEY_F(1)) {
		switch (ch) {
			case KEY_MOUSE:
				//
				onMouse(ch);
				break;
			case KEY_DOWN:
				/* Go to next field */
				form_driver(my_form, REQ_NEXT_FIELD);
				/* Go to the end of the present buffer */
				/* Leaves nicely at the last character */
				form_driver(my_form, REQ_END_LINE);
				break;
			case KEY_UP:
				/* Go to previous field */
				form_driver(my_form, REQ_PREV_FIELD);
				form_driver(my_form, REQ_END_LINE);
				break;
			case KEY_LEFT:
				form_driver(my_form, REQ_PREV_CHAR);
				break;
			case KEY_RIGHT:
				form_driver(my_form, REQ_NEXT_CHAR);
				break;
			case KEY_BACKSPACE:
			case 8:
				form_driver(my_form, REQ_DEL_PREV);
				break;
			default:
				/* If this is a normal character, it gets */
				/* printed */
				form_driver(my_form, ch);
				break;
		}
	}

	/* Un post form and free the memory */
	unpost_form(my_form);
	free_form(my_form);
	free_field(field[0]);
	free_field(field[1]);
	free_field(field[2]);
	free_field(field[3]);

	endwin();
}

void CWinQuiz::doModal()
{
	int ch;

	/* Initialize curses */
	initscr();
	start_color();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	/* Initialize few color paris */
	init_pair(1, COLOR_RED, COLOR_YELLOW);
	init_pair(2, COLOR_WHITE, COLOR_BLUE);
	init_pair(3, COLOR_WHITE, COLOR_CYAN);

	onInitialize();
	_innerstate = STATE_USER_CHOICE;	// Ready for user choice
	_pActive_Form = _pForm_;			// Active form

	while ((ch = wgetch(_pWin)) != KEY_F(1)) {
		switch (ch) {
			case KEY_MOUSE:
				onMouse(ch);
				break;
			default:
				onKeyboard(ch);
				break;
		}
	}

	onTearDown();
}

void CWinQuiz::Util::print_in_middle(WINDOW* win, int starty, int startx, int width, const char*string, chtype color) {
	int length, x, y;
	float temp;

	if (win == NULL) {
		win = stdscr;
	}

	getyx(win, y, x);

	if (startx != 0) {
		x = startx;
	}

	if (starty != 0) {
		y = starty;
	}

	if (width == 0) {
		width = 80;
	}

	length = strlen(string);
	temp = (width - length)/2;

	x = startx + (int)temp;
	wattron(win, color);
	mvwprintw(win, y, x, "%s", string);
	wattroff(win, color);
	refresh();
}

void CWinQuiz::Util::fill(WINDOW* win, int starty, int startx, int width, int height, int color_pair) {
	wattron(win, COLOR_PAIR(color_pair));

	for (int i=startx;i<width;i++)
	{
		for (int j=starty;j<height;j++)
		{
			mvwprintw(win,j,i," ");
		}
	}

	wrefresh(win);
	wattroff(win, COLOR_PAIR(color_pair));
}

void CWinQuiz::onInitialize()
{
	int nrows, ncols;
	/* Initialize the fields */
	_fields[0] = new_field(1, 40, _toppos + 4, 1, 0, 0);
	_fields[1] = new_field(1, 40, _toppos + 5, 1, 0, 0);
	_fields[2] = new_field(1, 40, _toppos + 6, 1, 0, 0);
	_fields[3] = new_field(1, 20, _toppos + 7, 1, 0, 0);
	_fields[4] = new_field(1, 20, _toppos + 7, 21, 0, 0);
	_fields[5] = NULL;

	/* Set field options */
	set_field_back(_fields[0], A_UNDERLINE);
	field_opts_off(_fields[0], O_AUTOSKIP);	/* Don't go to next field when this */
											/* Field is filled up */
	field_opts_off(_fields[0], O_ACTIVE);	/* Static field as label */

	set_field_back(_fields[1], A_UNDERLINE);
	field_opts_off(_fields[1], O_AUTOSKIP);
	set_field_fore(_fields[1], COLOR_PAIR(2));

	set_field_back(_fields[2], A_UNDERLINE);
	field_opts_off(_fields[2], O_AUTOSKIP);
	set_field_fore(_fields[2], COLOR_PAIR(2));

	// set_field_back(_fields[3], A_UNDERLINE);

	field_opts_on(_fields[3], O_STATIC);	// Button
	field_opts_off(_fields[3], O_EDIT);
	field_opts_off(_fields[3], O_AUTOSKIP);
	set_field_just(_fields[3], JUSTIFY_CENTER);
	set_field_fore(_fields[3], COLOR_PAIR(2));

	field_opts_on(_fields[4], O_STATIC);	// Button
	field_opts_off(_fields[4], O_EDIT);
	set_field_fore(_fields[4], COLOR_PAIR(2));

	// field_opts_on(_fields[5], O_STATIC);	// Button
	// set_field_fore(_fields[5], COLOR_PAIR(2));
	//set_field_just(_fields[4], JUSTIFY_CENTER);
	//et_field_back(_fields[4], COLOR_PAIR(3));

	// field_opts_on(_fields[5], O_STATIC);	// Button
	// field_opts_off(_fields[5], O_EDIT);
	// set_field_fore(_fields[5], COLOR_PAIR(3));
	// set_field_back(_fields[5], COLOR_PAIR(2));

	set_field_userptr(_fields[0], (void*)&_answer.answer1);
	set_field_userptr(_fields[1], (void*)&_answer.answer2);
	set_field_userptr(_fields[2], (void*)&_answer.answer3);

	set_field_buffer(_fields[0], 0, _answer.answer1.c_str());
	set_field_buffer(_fields[1], 0, _answer.answer2.c_str());
	set_field_buffer(_fields[2], 0, _answer.answer3.c_str());
	set_field_buffer(_fields[3], 0, "OK");
	set_field_buffer(_fields[4], 0, "Cancel");

	/* Create the form and post it */
	_pForm_ = new_form(_fields);

	/* Create the window and sub window */
	scale_form(_pForm_, &nrows, &ncols);

	/* Create the Window to be associated with the form */
	_pWin = newwin(nrows + 6, ncols + 2 , _toppos, _left);
	keypad(_pWin, TRUE);

	/* Set main window and sub window */
	_pWinSb = derwin(_pWin, nrows, ncols, 6, 1);

	set_form_win(_pForm_, _pWin);
	set_form_sub(_pForm_, _pWinSb);

	/* Print a border around the main Window and print a title */
	wborder(_pWin, '|', '|', '-', '-', '+', '+', '+', '+');
	Util::print_in_middle(_pWin, 1, 0, ncols + 4, "Doing quiz", COLOR_PAIR(1));
	Util::print_in_middle(_pWin, 2, 8, 0, _memory.code.c_str(), COLOR_PAIR(1));
	Util::print_in_middle(_pWin, 3, 8, 0, _memory.name.c_str(), COLOR_PAIR(1));
	Util::print_in_middle(_pWin, 4, 8, 0, _memory.author.c_str(), COLOR_PAIR(1));

	/* Print title */
	post_form(_pForm_);

	drawClock__();
	refresh();
}

void CWinQuiz::onTearDown()
{
	if (_pForm_)
	{
		unpost_form(_pForm_);
		free_form(_pForm_);
		_pForm_ = NULL;

		free_field(_fields[0]);
		free_field(_fields[1]);
		free_field(_fields[2]);
		free_field(_fields[3]);
	}

	if (_pFormError)
	{
		unpost_form(_pFormError);
		free_form(_pFormError);

		_pFormError = NULL;
		free_field(_fields_error[0]);
	}

	if (_pFormConfirm)
	{
		unpost_form(_pFormConfirm);
		free_field(_fieldconfirm[0]);
		free_field(_fieldconfirm[1]);
	}
}

void CWinQuiz::onLoadMemoryUnit(std::string scode)
{
	KERNEL->db()->getMemStick(string("Twenty"), _memory);
	_modelt = KERNEL->memorytest_()->generateTest(_memory);	// Model for test

	_answer.answer1 = "Answer1";
	_answer.answer2 = "Answer2";
	_answer.answer3 = "Answer3";

	_innerstate = STATE_USER_CHOICE;

	// TODO
	// enqueueMesage(MSG_LOADED_MEMQUIZ, (void*)&_memory);
}

void CWinQuiz::onTimer()
{
	if (_innerstate == STATE_DOING_QUIZ)
	{
		// Update Stopwatch
		drawClock__();	
	}
	else if (_innerstate == STATE_WAIT_FOR_START)
	{
		// Display Ready message		
		drawReadyMessage_();
	}
	else if (_innerstate == STATE_LOADING_QUIZ)
	{
		// drawLoading();
	}
}

void CWinQuiz::onKeyboard(int ch)
{
	std::string sActive = "Active Fields: ";

	switch (ch) {
		case KEY_DOWN:
			/* Go to next field */
			form_driver(_pForm_, REQ_NEXT_FIELD);
			/* Go to the end of the present buffer */
			/* Leaves nicely at the last character */
			form_driver(_pForm_, REQ_END_LINE);
			break;
		case KEY_UP:
			/* Go to previous field */
			form_driver(_pForm_, REQ_PREV_FIELD);
			form_driver(_pForm_, REQ_END_LINE);
			break;
		case KEY_LEFT:
			form_driver(_pForm_, REQ_PREV_CHAR);
			break;
		case KEY_RIGHT:
			form_driver(_pForm_, REQ_NEXT_CHAR);
			break;
		case KEY_BACKSPACE:
		case 8:
			form_driver(_pForm_, REQ_DEL_PREV);
			break;
		case 'h':
		case 'H':
		case 'a':
		case 'A':
		case 'd':
		case 'D':
		case 13:
		case KEY_ENTER:
			for (int i=0; _fields[i]; i++)
			{
				// if (field_status(_fields[i]) & O_STATIC)
				if (field_opts(_fields[i]) & O_ACTIVE)
				{
					char szTmp[20];
					sprintf(szTmp, "%d", i);
					sActive = sActive + szTmp + "; ";
					// mvprintw(LINES-3, 1, szTmp);
				}
			}
			break;
		default:
			/* If this is a normal character, it gets */
			/* printed */
			form_driver(_pForm_, ch);
			break;
	}

	char szBuff[255];
	char szBuff1[255];

	int status0 = field_status(_fields[0]);
	int status1 = field_status(_fields[1]);
	int status2 = field_status(_fields[2]);

	sprintf(szBuff, "Status=[%4.0X,%4.0X,%4.0X]; Buffers=[%s; %s; %s]",
								status0, status1, status2,
								field_buffer(_fields[0], 0),
								field_buffer(_fields[0], 1),
								field_buffer(_fields[0], 2));
	mvprintw(LINES-2, 1, szBuff);
	mvprintw(LINES-3, 1, sActive.c_str());

	refresh();
}

void CWinQuiz::onMouse(int ch)
{
	MEVENT m_event;					// Mouse events
	if(getmouse(&m_event) == OK)
	{
		/* When the user clicks left mouse button */
		if(m_event.bstate & BUTTON1_PRESSED)
		{
			move(m_event.y, m_event.x);
		}
	}
}

void CWinQuiz::onClose()
{
	// Send Event to main App
	this->app()->enqueueEvent(CURSESAPP_EVENT_WINQUIZ_CLOSE, (void*)this);	// Send event to main App
}

void CWinQuiz::onStartQuiz()
{
	_sw_seconds = 0;
}

void CWinQuiz::onCancel1Quiz()
{
}

void CWinQuiz::onCompleteTheQuiz()
{
}

void CWinQuiz::checkActiveFields()
{
	FIELD* fields[4];
	std::string strStatus = "";

	for (int i=0;i<4;i++)
	{
	}
}

void CWinQuiz::drawClock__()
{
	int minute = 0;
	int seconds = 0;
	_sw_seconds++;

	minute = _sw_seconds / 60;
	seconds = _sw_seconds % 60;

	if (minute > 99)
		minute = 99;
	// Draw the Clock
	mvwhline(_pWin, 5, 0, '_', _win_width_);
	mvwhline(_pWin, 7, 0, '_', _win_width_);

	mvwprintw(_pWin, 6, 2, "%2.0d:%2.0d", minute, seconds);
}

void CWinQuiz::drawLoading()
{
	if (_sw_seconds >= 0)
	{
		// Draw the Ready message
		mvwhline(_pWin, 5, 0, '_', _win_width_);
		mvwhline(_pWin, 7, 0, '_', _win_width_);

		mvwprintw(_pWin, 6, 2, "Loading Memory Unit... ");
	}
}

void CWinQuiz::drawReadyMessage_()
{
	int count_Down_;
	
	_sw_seconds --;
	count_Down_ = _sw_seconds / 2;

	if (count_Down_ >= 0)
	{
		// Draw the Ready message
		mvwhline(_pWin, 5, 0, '_', _win_width_);
		mvwhline(_pWin, 7, 0, '_', _win_width_);

		mvwprintw(_pWin, 6, 2, "Ready... %2.0d", count_Down_);
	}
}

int CWinQuiz::queueEvent_(int event, void*data)
{
	// Don't care about multithreading
	_q_messages.push(event);
	_q_data.push(data);
	return 0;
}

int CWinQuiz::processNextQueue_()	// 0; Success; 1-Failed; 2-No message on queue
{
	if (_q_messages.size() > 0)
	{
		int msg = _q_messages.front();
		void* data = _q_data.front();

		_q_messages.pop();
		_q_data.pop();

		step(msg, data);
	}
	return 0;
}

int CWinQuiz::_nextState_(int msg, void* data)
{
	int nwState = getNextState_(_innerstate, msg, data);

	//TODO: Do not allow call _nextState here - cause infinite loop
	// onEvent();		// Do not allow call _nextState here - cause infinite loop
	_innerstate = nwState;	// State change

	return 0;
}

int CWinQuiz::getNextState_(int currentState, int msg, void* data)
{
	int finalState_ = currentState;
	if (_innerstate == STATE_INIT && msg == MSG_DEFAULT)
	{
		finalState_ = STATE_LOADING_QUIZ;
	}
	else if (_innerstate == STATE_LOADING_QUIZ)
	{
		if (msg == MSG_LOADED_MEMQUIZ)
		{
			finalState_ = STATE_USER_CHOICE;
		}
		else if (msg == MSG_LOAD_FAILED_MEMQUIZ)
		{
			finalState_ = STATE_DISPLAY_ERROR;
		}
	}
	else if (_innerstate == STATE_USER_CHOICE)
	{
		if (msg == MSG_START_QUIZ)
		{
			finalState_ = STATE_WAIT_FOR_START;
		}
		else if (msg == MSG_CLOSE_FORM)
		{
			finalState_ = STATE_QUIT;
		}
	}
	else if (_innerstate == STATE_WAIT_FOR_START)
	{
		if (msg == MSG_DEFAULT)
		{
			finalState_ = STATE_DOING_QUIZ;
		}
	}
	else if (_innerstate == STATE_DOING_QUIZ)
	{
		if (msg == MSG_DEFAULT)
		{

		}
		else if (msg == MSG_CANCEL_QUIZ)
		{
			finalState_ = STATE_USER_CHOICE;
		}
		else if (msg == MSG_CLOSE_FORM)
		{
			finalState_ = STATE_QUIT;
		}
		else if (msg == MSG_TIMEOUT_QUIZ)
		{
			finalState_ = STATE_DISPLAY_SCORE;
		}
		else if (msg == MSG_FINISH_QUIZ)
		{
			finalState_ = STATE_CACULATE_QUIZ;
		}
	}
	else if (_innerstate == STATE_CANCEL_QUIZ)
	{

	}
	else if (_innerstate == STATE_CACULATE_QUIZ)
	{
		if (msg == MSG_DEFAULT)
		{
			finalState_ = STATE_DISPLAY_SCORE;
		}
	}
	else if (_innerstate == STATE_DISPLAY_SCORE)
	{	
		if (msg == MSG_START_QUIZ)
		{
			finalState_ = STATE_WAIT_FOR_START;
		}
		else if (msg == MSG_CLOSE_FORM)
		{
			finalState_ = STATE_QUIT;
		}
	}

	return finalState_;
}

// Inherited state machine
int CWinQuiz::currentState_()
{
	return _innerstate;
}

int CWinQuiz::step(int msg, void* data)		// Send event then process immediately
{
	int nwState = getNextState_(_innerstate, msg, data);

	if (nwState != _innerstate)
	{
		onLeaveState_(_innerstate);
		onTransition_(_innerstate, nwState, data);
		_innerstate = nwState;
		onEnterState_(nwState, data);
	}

	return nwState;
}

int CWinQuiz::onTransition_(int from_State_, int toState, void* data)
{
	/*
		States and Events:
		--------------------
		init
			--(msg_show)--> (onLoadQuiz_)loading_quiz 
		loading_quiz
			--(loaded_memquiz){ initDoingQuiz( [ init_curses, draw_quizform ]) } --> user choice
			--(load_failed_memquiz) { [ uninit_curses, drawErrorForm ] } --> display_error
		user_choice
			--(click_start_quiz_)--{ onWaitStart([ drawWaitstart ]) } ---> wait_start
			--(click_close_quiz_) { onCloseQuiz[ uninit_curses ] }---> quit
		wait_start
			--(timer ([ drawWaitStart ]) )--> wait_start
			--(timeup_wait_start{[ erase_wait_start_ ]})--> doing_quiz
		doing_quiz
			--(timer{ drawStopwatch } )---> doing_quiz
			--(timeup_ { finishDoQuiz_[stopStopwatch, calc_score_] } )--> display_score
			--(click_cancel_{ onClickCancel [ draw_quizform, erase_stop_watch_ ]})--> user_choice
			--(click_complete_quiz)---> confirm_finish_quiz
		confirm_finish_quiz
			--(click_Y { stopStopwatch, calc_score_ })--> display_score
			--(click_N { } )--> doing_quiz
		display_error
			--(click_close_quiz_ { uninit_curses } ) --> quit
		display_score
			--(click_restart)--> wait_start
			--(click_close_quiz_)--> quit
		quit
			--(show_form)--> loading_quiz
		<<any_state>>
			--(keyboard{ onKeyboard[] })--><<any_state>>
		<<any_state>>
			--(mouse{ onMouse[] })--><<any_state>>
	 */
	int FUNCID_ON_INITIALIZE = 1;
	int FUNCID_ON_LOAD_QUIZ = 2;

	TransitionRow transitions[] = 
	{
		TransitionRow(STATE_LOADING_QUIZ, 		STATE_USER_CHOICE, 		MSG_LOADED_MEMQUIZ, &			CWinQuiz::initDoingQuiz),
		TransitionRow(STATE_LOADING_QUIZ, 		STATE_DISPLAY_ERROR, 	MSG_LOAD_FAILED_MEMQUIZ, 		&CWinQuiz::drawErrorForm),
		TransitionRow(STATE_USER_CHOICE, 		STATE_WAIT_FOR_START,	MSG_CLICK_START_QUIZ, 			&CWinQuiz::onWaitStart),
		TransitionRow(STATE_USER_CHOICE, 		STATE_QUIT,				MSG_CLICK_CLOSE_QUIZ, 			&CWinQuiz::onCloseQuiz),
		TransitionRow(STATE_WAIT_FOR_START,		STATE_WAIT_FOR_START,			MSG_TIMER,						&CWinQuiz::drawWaitstart),
		TransitionRow(STATE_WAIT_FOR_START,		STATE_WAIT_FOR_START,			MSG_TIMEUP,						&CWinQuiz::erase_wait_start_),
		TransitionRow(STATE_DOING_QUIZ,			STATE_DOING_QUIZ,				MSG_TIMER,						&CWinQuiz::erase_wait_start_),
		TransitionRow(STATE_DOING_QUIZ,			STATE_DISPLAY_SCORE,			MSG_TIMEUP,						&CWinQuiz::finishDoQuiz_),
		TransitionRow(STATE_DOING_QUIZ,			STATE_USER_CHOICE,				MSG_CLICK_CANCEL,				&CWinQuiz::onClickCancel),
		TransitionRow(STATE_DOING_QUIZ,			STATE_CONFIRM_FINISH_QUIZ,		MSG_CLICK_COMPLETE_QUIZ,		&CWinQuiz::showConfirm),	// New

		TransitionRow(STATE_CONFIRM_FINISH_QUIZ,		STATE_DISPLAY_SCORE,		MSG_CLICK_CONFIRM_Y,		&CWinQuiz::finishDoQuiz_),
		TransitionRow(STATE_CONFIRM_FINISH_QUIZ,		STATE_DOING_QUIZ,			MSG_CLICK_CONFIRM_N,		&CWinQuiz::resumeDoQuiz_),	// New
		TransitionRow(STATE_DISPLAY_ERROR,				STATE_QUIT,					MSG_CLICK_CLOSE_QUIZ,		&CWinQuiz::uninit_curses),
		TransitionRow(STATE_DISPLAY_SCORE, 				STATE_WAIT_FOR_START,		MSG_CLICK_RESTART, 			&CWinQuiz::onRestartQuiz)	// New
		// TransitionRow(STATE_DISPLAY_SCORE, 				STATE_QUIT,					MSG_CLICK_CLOSE_QUIZ, 		&CWinQuiz::onRestartQuiz),		
	};

	int n = sizeof(_rows)/sizeof(TransitionRow); //sizeof(transitions)/sizeof(TransitionRow);
	for (int i = 0; i < n; ++i)
	{
		if (_rows[i].from_State_ == from_State_ && _rows[i].toState == toState)
		{
			return (this->*_rows[i].handler)(data);
		}
	}

	return 0;
}

int CWinQuiz::onEnterState_(int theoldstate, void* data)
{
	/*
		States and Events:
		--------------------
		init
			--(msg_show)--> (onLoadQuiz_)loading_quiz 
		loading_quiz
			--(loaded_memquiz){ initDoingQuiz( [ init_curses, draw_quizform ]) } --> user choice
			--(load_failed_memquiz) { [ uninit_curses, drawErrorForm ] } --> display_error
		user_choice
			--(click_start_quiz_)--{ onWaitStart([ drawWaitstart ]) } ---> wait_start
			--(click_close_quiz_) { onCloseQuiz[ uninit_curses ] }---> quit
		wait_start
			--(timer ([ drawWaitStart ]) )--> wait_start
			--(timeup_wait_start{[ erase_wait_start_ ]})--> doing_quiz
		doing_quiz
			--(timer{ drawStopwatch } )---> doing_quiz
			--(timeup_ { finishDoQuiz_[stopStopwatch, calc_score_] } )--> display_score
			--(click_cancel_{ onClickCancel [ draw_quizform, erase_stop_watch_ ]})--> user_choice
			--(click_complete_quiz)---> confirm_finish_quiz
		confirm_finish_quiz
			--(click_Y { stopStopwatch, calc_score_ })--> display_score
			--(click_N { } )--> doing_quiz
		display_error
			--(click_close_quiz_ { uninit_curses } ) --> quit
		display_score
			--(click_restart)--> wait_start
			--(click_close_quiz_)--> quit
		quit
			--(show_form)--> loading_quiz
		<<any_state>>
			--(keyboard{ onKeyboard[] })--><<any_state>>
		<<any_state>>
			--(mouse{ onMouse[] })--><<any_state>>
	 */

	if (currentState_() == STATE_LOADING_QUIZ && theoldstate == STATE_INIT)
	{
		onLoadQuiz_(data);
	}

	return 0;
}

int CWinQuiz::onLeaveState_(int state)
{
	return 0;
}


int CWinQuiz::onLoadQuiz_(void* data)		// Loading quiz
{
	string* pcode = (string*)data;

	KERNEL->db()->getMemStick((*pcode), _memory);
	_modelt = KERNEL->memorytest_()->generateTest(_memory);	// Model for test

	_answer.answer1 = "Answer1";
	_answer.answer2 = "Answer2";
	_answer.answer3 = "Answer3";
	return 0;
}

int CWinQuiz::init_curses(void* data)		// Init curses mode
{
	return 0;
}

int CWinQuiz::uninit_curses(void* data)
{
	return 0;
}

int CWinQuiz::initDoingQuiz(void* data)
{
	std::chrono::time_point<std::chrono::system_clock> tnow;
	tnow = std::chrono::system_clock::now();

	_memory.code = "Twenty";
	_memory.name = "Twenty quiz code";
	_memory.created_date = 0;
	_memory.author = "ducvd";

	_memory.listmemory_.push_back("1. World-class engineer");
	_memory.listmemory_.push_back("1.2. Solving 1000 stackexchange computer science's problems");
	_memory.listmemory_.push_back("1.3. Solving 1000 stackexchange mathematics's problems");
	_memory.listmemory_.push_back("2. True stamina 99");
	_memory.listmemory_.push_back("2.1. Easily run 3km in 20 minutes");
	_memory.listmemory_.push_back("2.2. Easily enjoy two ugly woman at the same time");
	_memory.listmemory_.push_back("3. Keep Ph-back");
	_memory.listmemory_.push_back("3.1. Find the right method to treat Ph's father for fully recover from disaster.");
	_memory.listmemory_.push_back("3.3. Give 60% stock of a company to her");

	_modelt = KERNEL->memorytest_()->generateTest(_memory);	// Model for test

	_answer.answer1 = "Answer1";
	_answer.answer2 = "Answer2";
	_answer.answer3 = "Answer3";
	return 0;
}

int CWinQuiz::draw_quizform(void* data)
{

	return 0;
}

int CWinQuiz::drawErrorForm(void* data)
{
	string* pstr_ = (string*)data;

	// Unpost old form, then post new form
	if (_pFormActive_ != NULL)
	{
		unpost_form(_pFormActive_);
		_pFormActive_ = NULL;
	}

	_pFormActive_ = _pFormError;
	set_form_win(_pFormActive_, _pWin);
	set_form_sub(_pFormActive_, _pWinSb);
	post_form(_pFormActive_);

	return 0;
}

int CWinQuiz::drawWaitstart(void* data)
{
	int minute = 0;
	int seconds = 0;

	minute = _sw_seconds / 60;
	seconds = _sw_seconds % 60;

	if (minute > 99) {
		minute = 99;
	}
	// Draw the Clock
	mvwhline(_pWin, 5, 0, '_', _win_width_);
	mvwhline(_pWin, 7, 0, '_', _win_width_);

	mvwprintw(_pWin, 6, 2, "%2.0d:%2.0d", minute, seconds);

	return 0;
}

int CWinQuiz::erase_wait_start_(void* data)
{

	mvwhline(_pWin, 5, 0, ' ', _win_width_);
	mvwhline(_pWin, 7, 0, ' ', _win_width_);
	mvwprintw(_pWin, 6, 2, "    ");
	return 0;
}

int CWinQuiz::drawStopwatch(void* data)
{
	int minute = 0;
	int seconds = 0;

	minute = _sw_seconds / 60;
	seconds = _sw_seconds % 60;

	if (minute > 99) {
		minute = 99;
	}
	// Draw the Clock
	mvwhline(_pWin, 5, 0, '_', _win_width_);
	mvwhline(_pWin, 7, 0, '_', _win_width_);

	mvwprintw(_pWin, 6, 2, "%2.0d:%2.0d", minute, seconds);
	return 0;
}

int CWinQuiz::stopStopwatch(void* data)
{
	mvwhline(_pWin, 5, 0, ' ', _win_width_);
	mvwhline(_pWin, 7, 0, ' ', _win_width_);
	mvwprintw(_pWin, 6, 2, "    ");
	return 0;
}

int CWinQuiz::finishDoQuiz_(void* data)
{
	calc_score_(data);

	int score1 = 5;
	int score2 = 7;
	int score3 = 7;
	
	return 0;
}


int CWinQuiz::calc_score_(void* data)
{
	return 0;
}


int CWinQuiz::resumeDoQuiz_(void* data)
{
	return 0;
}


int CWinQuiz::onRestartQuiz(void* data)
{
	return 0;
}


int CWinQuiz::onWaitStart(void* data)
{
	return 0;
}

int CWinQuiz::showConfirm(void* data)
{
	return 0;
}

int CWinQuiz::onCloseQuiz(void* data)
{
	// Draw a form here
	if (_pActive_Form != _pFormConfirm)
	{
		unpost_form(_pActive_Form);
		post_form(_pFormConfirm);

		_pActive_Form = _pFormConfirm;
		refresh();
	}

	return 0;
}

int CWinQuiz::onClickCancel(void* data)
{
	return 0;
}

int CWinQuiz::onEvent(int event, void* data)
{
	return 0;
}

int CWinQuiz::enqueueEvent(int event, void* data)
{
	_q_data.push(data);
	_q_messages.push(event);
	return 0;
}
