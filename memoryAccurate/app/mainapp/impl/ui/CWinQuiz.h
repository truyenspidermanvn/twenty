/*
 * CWinQuiz.h
 *
 *  Created on: Dec 12, 2017
 *      Author: spider
 */

#ifndef APP_MAINAPP_IMPL_UI_CWINQUIZ_H_
#define APP_MAINAPP_IMPL_UI_CWINQUIZ_H_

#include "mainapp/interface/ui/ICursesWinQuiz.h"
#include "mainapp/interface/model/quiz_model.h"
#include <form.h>

#define WQZ_FLAG_IS_QUIZING 0x00000001

class CWinQuiz: public ICursesWinQuiz {
public:
	CWinQuiz();
	virtual ~CWinQuiz();

	// Inherit virtual functions
	void init_curses_mode() {}
	int onEvent(int event, void* data) { return 1; }
	int enqueueEvent(int event, void*data = NULL) { return 1; }
	void tear_down() {}
	ICursesApp* app() { return NULL; }

	void doModal();

	class Util
	{
	public:
		static void print_in_middle(WINDOW* win, int starty, int startx, int width, const char* string, chtype color);
		static void fill(WINDOW* win, int starty, int startx, int width, int height, int color_pair);
	};
protected:
	// Supported functions
	void checkActiveFields();

	// Event functions
	void onInitialize();	// Init windows
	void onTearDown();		// Break ncurses

	void onStartQuiz();
	void onCancel1Quiz();
	void onCompleteTheQuiz();

	void onTimer();			// Timer to
	void onClose();			//

	void onKeyboard(int ch);
	void onMouse(int ch);

private:
	FORM* pForm;
	WINDOW* pWin;

	MemoryStick _memory;
	QuizTestModel _modelt; 	// Test model
	QuizAnswer _answer;

	int _innerstate = 0;		
	int _sw_seconds;	// Stopwach seconds	
};

#endif /* APP_MAINAPP_IMPL_UI_CWINQUIZ_H_ */
