//This file is part of FingerText, a notepad++ snippet plugin.
//
//FingerText is released under MIT License.
//
//MIT license
//
//Copyright (C) 2011 by Tom Lam
//
//Permission is hereby granted, free of charge, to any person 
//obtaining a copy of this software and associated documentation 
//files (the "Software"), to deal in the Software without 
//restriction, including without limitation the rights to use, 
//copy, modify, merge, publish, distribute, sublicense, and/or 
//sell copies of the Software, and to permit persons to whom the 
//Software is furnished to do so, subject to the following 
//conditions:
//
//The above copyright notice and this permission notice shall be 
//included in all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
//EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
//OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
//NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
//HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
//WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
//DEALINGS IN THE SOFTWARE.

#include "PluginDefinition.h"

// Notepad++ API stuffs
FuncItem funcItem[MENU_LENGTH];     // The menu item data that Notepad++ needs
NppData nppData;                    // The data for plugin command and sending message to notepad++
HANDLE g_hModule;                   // the hModule from pluginInit for initializing dialogs

// Sqlite3
sqlite3 *g_db;                      // For Sqlite3 
bool     g_dbOpen;                  // For Sqlite3 


// Paths
wchar_t g_basePath[MAX_PATH];
TCHAR g_ftbPath[MAX_PATH];
TCHAR g_fttempPath[MAX_PATH];
TCHAR g_currentFocusPath[MAX_PATH];

// Config object
PluginConfig pc;

// Dialogs
DockingDlg snippetDock;
DummyStaticDlg	dummyStaticDlg;

// Need a record for all the cmdIndex that involve a dock or a shortkey
int g_snippetDockIndex;
int g_tabActivateIndex;


//TODO: should use vector of String here
struct SnipIndex 
{
    char* triggerText;
    char* scope;
    char* content;    
};

SnipIndex* g_snippetCache;

bool g_modifyResponse;
bool g_enable;
bool g_editorView;
int g_selectionMonitor;
bool g_rectSelection;

int g_editorLineCount;

int g_lastTriggerPosition;
std::string g_customClipBoard;

// For option hotspot
bool g_optionMode;
int g_optionStartPosition;
int g_optionEndPosition;
int g_optionCurrent;
std::vector<std::string> g_optionArray;

// List of acceptable tagSigns
char *g_tagSignList[] = {"$[![","$[1[","$[2[","$[3["};
char *g_tagTailList[] = {"]!]","]1]","]2]","]3]"};
int g_listLength = 4;

//For params insertion
std::vector<std::string> g_hotspotParams;

//For SETWIN
HWND g_tempWindowHandle;
wchar_t* g_tempWindowKey;

//TODO: Add icon to messageboxes

// Initialize your plugin data here. Called while plugin loading. This runs before setinfo.
void pluginInit(HANDLE hModule)
{
    g_hModule = hModule;  // For dialogs initialization
}

// Initialization of plugin commands
void commandMenuInit()
{
    ShortcutKey *shKey = setShortCutKey(false,false,false,VK_TAB);

    g_tabActivateIndex = setCommand(TEXT("Trigger Snippet/Navigate to Hotspot"), tabActivate, shKey);
    setCommand();
    g_snippetDockIndex = setCommand(TEXT("Toggle On/off SnippetDock"), showSnippetDock);
    setCommand(TEXT("Toggle On/Off FingerText"), toggleDisable);
    setCommand(TEXT("Create Snippet from Selection"),  selectionToSnippet);
    setCommand(TEXT("Import Snippets"), importSnippets);
    setCommand(TEXT("Export Snippets"), exportSnippetsOnly);
    setCommand(TEXT("Export and Delete All Snippets"), exportAndClearSnippets);
    setCommand();
    setCommand(TEXT("TriggerText Completion"), tagComplete);
    setCommand(TEXT("Insert a hotspot"), insertHotSpotSign);
    setCommand();
    setCommand(TEXT("Settings"), showSettings);
    setCommand(TEXT("Quick Guide"), showHelp);
    setCommand(TEXT("About"), showAbout);
    setCommand();
    setCommand(TEXT("Testing"), testing);
    setCommand(TEXT("Testing2"), testing2);
}



void dialogsInit()
{
    snippetDock.init((HINSTANCE)g_hModule, NULL);
    dummyStaticDlg.init((HINSTANCE)g_hModule, nppData);
}

void pathInit()
{
    // Get the config folder of notepad++ and append the plugin name to form the root of all config files
    ::SendMessage(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(g_basePath));
    ::_tcscat_s(g_basePath,TEXT("\\"));
    ::_tcscat_s(g_basePath,TEXT(PLUGIN_NAME));
    if (PathFileExists(g_basePath) == false) ::CreateDirectory(g_basePath, NULL);
    
    // Initialize the files needed (ini and database paths are initalized in configInit and databaseInit)
    ::_tcscpy_s(g_fttempPath,g_basePath);
    ::_tcscat_s(g_fttempPath,TEXT("\\"));
    ::_tcscat_s(g_fttempPath,TEXT(PLUGIN_NAME));
    ::_tcscat_s(g_fttempPath,TEXT(".fttemp"));
    if (PathFileExists(g_fttempPath) == false) emptyFile(g_fttempPath);

    ::_tcscpy_s(g_ftbPath,g_basePath);
    ::_tcscat_s(g_ftbPath,TEXT("\\SnippetEditor.ftb"));
    if (PathFileExists(g_ftbPath) == false) emptyFile(g_ftbPath);
    
}

void configInit()
{
    ::_tcscpy_s(pc.iniPath,g_basePath);
    ::_tcscat_s(pc.iniPath,TEXT("\\"));
    ::_tcscat_s(pc.iniPath,TEXT(PLUGIN_NAME));
    ::_tcscat_s(pc.iniPath,TEXT(".ini"));
    if (PathFileExists(pc.iniPath) == false) emptyFile(pc.iniPath);

    pc.configSetUp();
}


void dataBaseInit()
{
    char* dataBasePath = new char[MAX_PATH];
    char* basePath = toCharArray(g_basePath);
    strcpy(dataBasePath,basePath);
    strcat(dataBasePath,"\\");
    strcat(dataBasePath,PLUGIN_NAME);
    strcat(dataBasePath,".db3");
    delete [] basePath;

    if (sqlite3_open(dataBasePath, &g_db))
    {
        g_dbOpen = false;
        showMessageBox(TEXT("Cannot find or open database file in config folder"));
    } else
    {
        g_dbOpen = true;
    }

    sqlite3_stmt *stmt;

    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, 
    "CREATE TABLE snippets (tag TEXT, tagType TEXT, snippet TEXT)"
    , -1, &stmt, NULL))
    
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    delete [] dataBasePath; 
}




void variablesInit()
{
    g_customClipBoard = "";   

    g_modifyResponse = true;
    g_enable = true;

    g_selectionMonitor = 1;
    g_rectSelection = false;

    // For option hotspot
    turnOffOptionMode();
    g_optionStartPosition = 0;
    g_optionEndPosition = 0;
    g_optionCurrent = 0;
    
    g_lastTriggerPosition = 0;
    
    g_snippetCache = new SnipIndex [pc.configInt[SNIPPET_LIST_LENGTH]];

}


void nppReady()
{
    pc.upgradeMessage();

    if (pc.configInt[FORCE_MULTI_PASTE]) ::SendScintilla(SCI_SETMULTIPASTE,1,0); 

    if (snippetDock.isVisible()) updateDockItems(); //snippetHintUpdate();
}



void pluginShutdown()  // function is triggered when NPPN_SHUTDOWN fires  
{
    
    pc.configCleanUp();

    delete [] g_snippetCache;
    
    if (g_dbOpen)
    {
        sqlite3_close(g_db);  // This close the database when the plugin shutdown.
        g_dbOpen = false;
    }
}

// command shortcut clean up
void commandMenuCleanUp()
{
    delete funcItem[g_tabActivateIndex]._pShKey;
	// Don't forget to deallocate your shortcut here
}

void pluginCleanUp()
{
    //TODO: think about how to save the parameters for the next session during clean up    
}




// Functions for Fingertext
void toggleDisable()
{
    if (g_enable)
    {
        // TODO: refactor all the message boxes to a separate function
        showMessageBox(TEXT("FingerText is disabled"));
        //::MessageBox(nppData._nppHandle, TEXT("FingerText is disabled"), TEXT(PLUGIN_NAME), MB_OK);
        g_enable = false;
    } else
    {
        showMessageBox(TEXT("FingerText is enabled"));
        //::MessageBox(nppData._nppHandle, TEXT("FingerText is enabled"), TEXT(PLUGIN_NAME), MB_OK);
        g_enable = true;
    }
    updateMode();
}

void openDummyStaticDlg(void)
{
	dummyStaticDlg.doDialog();
}

char *findTagSQLite(char *tag, char *tagCompare, bool similar)
{
    //alertCharArray(tagCompare);
	char *expanded = NULL;
	sqlite3_stmt *stmt;

    // First create the SQLite SQL statement ("prepare" it for running)
    char *sqlitePrepareStatement;
    if (similar)
    {
        sqlitePrepareStatement = "SELECT tag FROM snippets WHERE tagType LIKE ? AND tag LIKE ? ORDER BY tag";
    } else
 
    {
        sqlitePrepareStatement = "SELECT snippet FROM snippets WHERE tagType LIKE ? AND tag LIKE ? ORDER BY tag";
    }
    
    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, sqlitePrepareStatement, -1, &stmt, NULL))
	{
        sqlite3_bind_text(stmt, 1, tagCompare, -1, SQLITE_STATIC);

        if (similar)
        {
            char similarTag[MAX_PATH]="";
            if (pc.configInt[INCLUSIVE_TRIGGERTEXT_COMPLETION]==1) strcat(similarTag,"%");
            strcat(similarTag,tag);
            strcat(similarTag,"%");
            //::SendMessage(getCurrentScintilla(),SCI_INSERTTEXT,0,(LPARAM)similarTag);
            sqlite3_bind_text(stmt, 2, similarTag, -1, SQLITE_STATIC);
        } else
        {
		    sqlite3_bind_text(stmt, 2, tag, -1, SQLITE_STATIC);
        }

		// Run the query with sqlite3_step
		if(SQLITE_ROW == sqlite3_step(stmt))  // SQLITE_ROW 100 sqlite3_step() has another row ready
		{
			const char* expandedSQL = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)); // The 0 here means we only take the first column returned. And it is the snippet as there is only one column
			expanded = new char[strlen(expandedSQL)*4 + 1];
			strcpy(expanded, expandedSQL);
		}
	}
    // Close the SQLite statement, as we don't need it anymore
	// This also has the effect of free'ing the result from sqlite3_column_text 
	// (i.e. in our case, expandedSQL)
	sqlite3_finalize(stmt);
	return expanded; //remember to delete the returned expanded after use.
}



void selectionToSnippet()
{
    g_selectionMonitor--;
    
    //pc.configInt[EDITOR_CARET_BOUND]--;
    
    //HWND curScintilla = getCurrentScintilla();
    int selectionEnd = ::SendScintilla(SCI_GETSELECTIONEND,0,0);
    int selectionStart = ::SendScintilla(SCI_GETSELECTIONSTART,0,0);
    bool withSelection = false;

    char* selection;
    
    if (selectionEnd>selectionStart)
    {
        
        sciGetText(&selection,selectionStart,selectionEnd);
        //selection = new char [selectionEnd - selectionStart +1];
        //::SendScintilla(SCI_GETSELTEXT,0, reinterpret_cast<LPARAM>(selection));
        withSelection = true;
    } else
    {
        selection = "New snippet is here.\r\nNew snippet is here.\r\nNew snippet is here.\r\n";
    }
    
    //::SendMessage(curScintilla,SCI_GETSELTEXT,0, reinterpret_cast<LPARAM>(selection));
    
    if (!::SendMessage(nppData._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)g_ftbPath))
    {
        ::SendMessage(nppData._nppHandle, NPPM_DOOPEN, 0, (LPARAM)g_ftbPath);
    } 
    
    
    //curScintilla = getCurrentScintilla();
    
    //TODO: consider using YES NO CANCEL dialog in promptsavesnippet
    promptSaveSnippet(TEXT("Do you wish to save the current snippet before creating a new one?"));
    
    
    ::SendScintilla(SCI_CLEARALL,0,0);
    ::SendScintilla(SCI_INSERTTEXT,::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)"------ FingerText Snippet Editor View ------\r\n");
    ::SendScintilla(SCI_INSERTTEXT,::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)"triggertext\r\nGLOBAL\r\n");
    ::SendScintilla(SCI_INSERTTEXT,::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)selection);
    ::SendScintilla(SCI_INSERTTEXT,::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)"[>END<]");

    g_editorView = 1;
    
    //updateDockItems(false,false);
    //updateMode();
    //refreshAnnotation();
    ::SendScintilla(SCI_GOTOLINE,1,0);
    ::SendScintilla(SCI_WORDRIGHTEXTEND,1,0);

    if (withSelection) delete [] selection;
    ::SendScintilla(SCI_EMPTYUNDOBUFFER,0,0);
    
    //pc.configInt[EDITOR_CARET_BOUND]++;
    g_selectionMonitor++;
}

void editSnippet()
{
    TCHAR* bufferWide;
    snippetDock.getSelectText(bufferWide);
    char* buffer = toCharArray(bufferWide);
    buffer = quickStrip(buffer, ' ');

    int scopeLength = ::strchr(buffer,'>') - buffer - 1;
    int triggerTextLength = strlen(buffer)-scopeLength - 2;
    char* tempTriggerText = new char [ triggerTextLength+1];
    char* tempScope = new char[scopeLength+1];
    
    strncpy(tempScope,buffer+1,scopeLength);
    tempScope[scopeLength] = '\0';
    strncpy(tempTriggerText,buffer+1+scopeLength+1,triggerTextLength);
    tempTriggerText[triggerTextLength] = '\0';
    
    delete [] buffer;

    sqlite3_stmt *stmt;
    
    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "SELECT snippet FROM snippets WHERE tagType = ? AND tag = ?", -1, &stmt, NULL))
	{
		// Then bind the two ? parameters in the SQLite SQL to the real parameter values
		sqlite3_bind_text(stmt, 1, tempScope , -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, tempTriggerText, -1, SQLITE_STATIC);
		// Run the query with sqlite3_step
		if(SQLITE_ROW == sqlite3_step(stmt))  // SQLITE_ROW 100 sqlite3_step() has another row ready
		{
			const char* snippetText = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)); // The 0 here means we only take the first column returned. And it is the snippet as there is only one column
    
            // After loading the content, switch to the editor buffer and promput for saving if needed
            if (!::SendMessage(nppData._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)g_ftbPath))
            {
                ::SendMessage(nppData._nppHandle, NPPM_DOOPEN, 0, (LPARAM)g_ftbPath);
            }
            //HWND curScintilla = getCurrentScintilla();
            promptSaveSnippet(TEXT("Do you wish to save the current snippet before editing anotoher one?"));
            ::SendScintilla(SCI_CLEARALL,0,0);
               
            //::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
            ::SendScintilla(SCI_INSERTTEXT, ::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)"------ FingerText Snippet Editor View ------\r\n");
            ::SendScintilla(SCI_INSERTTEXT, ::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)tempTriggerText);
            ::SendScintilla(SCI_INSERTTEXT, ::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)"\r\n");
            ::SendScintilla(SCI_INSERTTEXT, ::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)tempScope);
            ::SendScintilla(SCI_INSERTTEXT, ::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)"\r\n");
    
            ::SendScintilla(SCI_INSERTTEXT, ::SendScintilla(SCI_GETLENGTH,0,0), (LPARAM)snippetText);
    
            g_editorView = true;
            refreshAnnotation();
		}
	}
    
	sqlite3_finalize(stmt);
    
    ::SendScintilla(SCI_SETSAVEPOINT,0,0);
    ::SendScintilla(SCI_EMPTYUNDOBUFFER,0,0);
    delete [] tempTriggerText;
    delete [] tempScope;
    delete [] bufferWide;
}

void deleteSnippet()
{
    TCHAR* bufferWide;
    snippetDock.getSelectText(bufferWide);
    char* buffer = toCharArray(bufferWide);
    buffer = quickStrip(buffer, ' ');

    int scopeLength = ::strchr(buffer,'>') - buffer - 1;
    int triggerTextLength = strlen(buffer)-scopeLength - 2;
    char* tempTriggerText = new char [ triggerTextLength+1];
    char* tempScope = new char[scopeLength+1];
    
    strncpy(tempScope,buffer+1,scopeLength);
    tempScope[scopeLength] = '\0';
    strncpy(tempTriggerText,buffer+1+scopeLength+1,triggerTextLength);
    tempTriggerText[triggerTextLength] = '\0';
    
    delete [] buffer;

    sqlite3_stmt *stmt;
    
    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "DELETE FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
    {
        sqlite3_bind_text(stmt, 1, tempScope, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, tempTriggerText, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    
    updateDockItems(false,false);

    delete [] tempTriggerText;
    delete [] tempScope;
    delete [] bufferWide;
}

bool getLineChecked(char **buffer, int lineNumber, TCHAR* errorText)
{
    // TODO: and check for more error, say the triggertext has to be one word
    bool problemSnippet = false;

    ::SendScintilla(SCI_GOTOLINE,lineNumber,0);

    int tagPosStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);

    int tagPosEnd;
    
    if (lineNumber == 3)
    {
        tagPosEnd = ::SendScintilla(SCI_GETLENGTH,0,0);
    } else
    {
        int tagPosLineEnd = ::SendScintilla(SCI_GETLINEENDPOSITION,lineNumber,0);

        char* wordChar;
        if (lineNumber==2)
        {
            wordChar = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:.";
            
        } else //if (lineNumber==1)
        {
            wordChar = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
        }
        ::SendScintilla(SCI_SETWORDCHARS, 0, (LPARAM)wordChar);
        tagPosEnd = ::SendScintilla(SCI_WORDENDPOSITION,tagPosStart,0);
        ::SendScintilla(SCI_SETCHARSDEFAULT, 0, 0);
        //::SendMessage(curScintilla,SCI_SEARCHANCHOR,0,0);
        //::SendMessage(curScintilla,SCI_SEARCHNEXT,0,(LPARAM)" ");
        //tagPosEnd = ::SendMessage(curScintilla,SCI_GETCURRENTPOS,0,0);
        if ((tagPosEnd>tagPosLineEnd) || (tagPosEnd-tagPosStart<=0))
        {
            //blank
            ::SendScintilla(SCI_GOTOLINE,lineNumber,0);
            showMessageBox(errorText);
            //::MessageBox(nppData._nppHandle, errorText, TEXT(PLUGIN_NAME), MB_OK);
            problemSnippet = true;
            
        } else if (tagPosEnd<tagPosLineEnd)
        {
            // multi
            ::SendScintilla(SCI_GOTOLINE,lineNumber,0);
            showMessageBox(errorText);
            //::MessageBox(nppData._nppHandle, errorText, TEXT(PLUGIN_NAME), MB_OK);
            problemSnippet = true;
        }
    }

    if (lineNumber == 3)
    {
        ::SendScintilla(SCI_GOTOPOS,tagPosStart,0);
        int spot = searchNext("[>END<]");
        if (spot<0)
        {
            showMessageBox(TEXT("You should put an \"[>END<]\" (without quotes) at the end of your snippet content."));
            //::MessageBox(nppData._nppHandle, TEXT("You should put an \"[>END<]\" (without quotes) at the end of your snippet content."), TEXT(PLUGIN_NAME), MB_OK);
            problemSnippet = true;
        }
    }

    //::SendScintilla(SCI_SETSELECTION,tagPosStart,tagPosEnd);
    //*buffer = new char[tagPosEnd-tagPosStart + 1];
    //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(*buffer));

    sciGetText(&*buffer,tagPosStart,tagPosEnd);

    return problemSnippet;
}

//TODO: saveSnippet() and importSnippet() need refactoring sooooooo badly..................
void saveSnippet()
{
    //HWND curScintilla = getCurrentScintilla();
    g_selectionMonitor--;
    int docLength = ::SendScintilla(SCI_GETLENGTH,0,0);
    // insert a space at the end of the doc so the ::SendMessage(curScintilla,SCI_SEARCHNEXT,0,(LPARAM)" "); will not get into error
    // TODO: Make sure that it is not necessary to keep this line
    //::SendMessage(curScintilla, SCI_INSERTTEXT, docLength, (LPARAM)" ");
    
    bool problemSnippet = false;

    char* tagText;
    char* tagTypeText;
    char* snippetText;

    if (getLineChecked(&tagText,1,TEXT("TriggerText cannot be blank, and it can only contain alphanumeric characters (no spaces allowed)"))==true) problemSnippet = true;
    if (getLineChecked(&tagTypeText,2,TEXT("Scope cannot be blank, and it can only contain alphanumeric characters and/or period."))==true) problemSnippet = true;
    if (getLineChecked(&snippetText,3,TEXT("Snippet Content cannot be blank."))==true) problemSnippet = true;
    
    ::SendScintilla(SCI_SETSELECTION,docLength,docLength+1); //Take away the extra space added
    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");

    if (!problemSnippet)
    {
        // checking for existing snippet 
        sqlite3_stmt *stmt;

        if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "SELECT snippet FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
        {
            sqlite3_bind_text(stmt, 1, tagTypeText, -1, SQLITE_STATIC);
		    sqlite3_bind_text(stmt, 2, tagText, -1, SQLITE_STATIC);
            if(SQLITE_ROW == sqlite3_step(stmt))
            {
                sqlite3_finalize(stmt);
                int messageReturn = showMessageBox(TEXT("Snippet exists, overwrite?"),MB_YESNO);
                //int messageReturn = ::MessageBox(nppData._nppHandle, TEXT("Snippet exists, overwrite?"), TEXT(PLUGIN_NAME), MB_YESNO);
                if (messageReturn==IDNO)
                {
                    delete [] tagText;
                    delete [] tagTypeText;
                    delete [] snippetText;
                    // not overwrite
                    showMessageBox(TEXT("The Snippet is not saved."));
                    //::MessageBox(nppData._nppHandle, TEXT("The Snippet is not saved."), TEXT(PLUGIN_NAME), MB_OK);
                    //::SendMessage(curScintilla, SCI_GOTOPOS, 0, 0);
                    //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)" ");
                    ::SendScintilla(SCI_SETSELECTION, 0, 1);
                    ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)"-");
                    ::SendScintilla(SCI_GOTOPOS, 0, 0);
                
                    return;

                } else
                {
                    // delete existing entry
                    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "DELETE FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
                    {
                        sqlite3_bind_text(stmt, 1, tagTypeText, -1, SQLITE_STATIC);
		                sqlite3_bind_text(stmt, 2, tagText, -1, SQLITE_STATIC);
                        sqlite3_step(stmt);
                    
                    } else
                    {
                        showMessageBox(TEXT("Cannot write into database."));
                        //::MessageBox(nppData._nppHandle, TEXT("Cannot write into database."), TEXT(PLUGIN_NAME), MB_OK);
                    }
                
                }

            } else
            {
                sqlite3_finalize(stmt);
            }
        }
    
        if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "INSERT INTO snippets VALUES(?,?,?)", -1, &stmt, NULL))
	    {
		    // Then bind the two ? parameters in the SQLite SQL to the real parameter values
		    sqlite3_bind_text(stmt, 1, tagText, -1, SQLITE_STATIC);
		    sqlite3_bind_text(stmt, 2, tagTypeText, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, snippetText, -1, SQLITE_STATIC);
    
		    // Run the query with sqlite3_step
		    sqlite3_step(stmt); // SQLITE_ROW 100 sqlite3_step() has another row ready
            showMessageBox(TEXT("The Snippet is saved."));
            //::MessageBox(nppData._nppHandle, TEXT("The Snippet is saved."), TEXT(PLUGIN_NAME), MB_OK);
	    }
        sqlite3_finalize(stmt);
        ::SendScintilla(SCI_SETSAVEPOINT,0,0);
    }
    delete [] tagText;
    delete [] tagTypeText;
    delete [] snippetText;
    
    updateDockItems(false,false);
    g_selectionMonitor++;
}

void restoreTab(int &posCurrent, int &posSelectionStart, int &posSelectionEnd)
{
    // restoring the original tab action
    ::SendScintilla(SCI_GOTOPOS,posCurrent,0);
    ::SendScintilla(SCI_SETSELECTION,posSelectionStart,posSelectionEnd);
    ::SendScintilla(SCI_TAB,0,0);	
}

//TODO: refactor searchPrevMatchedSign and searchNextMatchedTail
int searchPrevMatchedSign(char* tagSign, char* tagTail)
{
    //This function works when the caret is at the beginning of tagtail
    // it return the position at the beginning of the tagsign if found
    int signSpot = -1;
    int tailSpot = -1;
    int unmatchedTail = 0;
    
    do
    {
        
        int posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
        tailSpot = searchPrev(tagTail);
        ::SendScintilla(SCI_GOTOPOS,posCurrent,0);
        signSpot = searchPrev(tagSign);
        if (signSpot == -1) 
        {
            
            return -1;
        }

        if ((signSpot > tailSpot) && (unmatchedTail == 0))
        {
            
            return signSpot;
        } else if (signSpot > tailSpot)
        {
            ::SendScintilla(SCI_GOTOPOS,signSpot,0);
            unmatchedTail--;
        } else
        {
            ::SendScintilla(SCI_GOTOPOS,tailSpot,0);
            unmatchedTail++;
        
        }
        
    } while (1);
    return -1;
}

int searchNextMatchedTail(char* tagSign, char* tagTail)
{

    // TODO: this function is not returning the position correctly, but it stop at the place where is find the tail
    // This function is tested to work when the position is at the end of tagSign
    // And this return the position at the END of tailsign, if found
   
    int signSpot = -1;
    int tailSpot = -1;
    int unmatchedSign = 0;
    
    int signLength = strlen(tagSign);
    int tailLength = strlen(tagTail);

    do
    {
        
        int posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
        signSpot = searchNext(tagSign);
        if (signSpot != -1) signSpot = signSpot+signLength;
        ::SendScintilla(SCI_GOTOPOS,posCurrent,0);
        tailSpot = searchNext(tagTail);
        if (tailSpot != -1) tailSpot = tailSpot+tailLength;
        
        //alertNumber(tailSpot);
        //alertNumber(signSpot);
        //alertNumber(unmatchedSign); 
        
        if (tailSpot == -1) return -1;

        if (((signSpot > tailSpot) || ((signSpot == -1)  && (tailSpot>=0))) && (unmatchedSign == 0))
        {
            return tailSpot;
        } else if (((signSpot > tailSpot) || ((signSpot == -1)  && (tailSpot>=0))))
        {
            ::SendScintilla(SCI_GOTOPOS,tailSpot,0);
            unmatchedSign--;
        } else
        {
            ::SendScintilla(SCI_GOTOPOS,signSpot,0);
            unmatchedSign++;
        }
        
    } while (1);
    return -1;
}

bool dynamicHotspot(int &startingPos, char* tagSign, char* tagTail)
{
    
    int checkPoint = startingPos;    
    bool normalSpotTriggered = false;
    
    //char tagSign[] = "$[![";
    //int tagSignLength = strlen(tagSign);
    //char tagTail[] = "]!]";
    //int tagTailLength = strlen(tagTail);

    //int tagSignLength = strlen(tagSign);
    int tagSignLength = 4;

    char* hotSpotText;
    char* hotSpot;
    int spot = -1;
    int spotComplete = -1;
    int spotType = 0;

    int limitCounter = 0;
    do 
    {
        
        //alertString(g_hotspotParams[0]);
        ::SendScintilla(SCI_GOTOPOS,checkPoint,0);
        spot = searchNext(tagTail);   // Find the tail first so that nested snippets are triggered correctly
        //spot = searchNext(curScintilla, tagSign);
        
        if (spot>=0)
	    {
            checkPoint = ::SendScintilla(SCI_GETCURRENTPOS,0,0)+1;
            spotComplete = -1;
            //spotComplete = searchPrev(tagSign);

            spotComplete = searchPrevMatchedSign(tagSign,tagTail);
            
            if (spotComplete>=0)
            {

                int firstPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                int secondPos = 0;
                
                spotType = grabHotSpotContent(&hotSpotText, &hotSpot, firstPos, secondPos, tagSignLength, tagTail);
                
                if (spotType>0)
                {

                    if (!normalSpotTriggered)
                    {
                        ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)hotSpotText+5);
                 
                        ::SendScintilla(SCI_GOTOPOS,secondPos+3,0); // TODO: Check whether this GOTOPOS is necessary
                
                        //TODO: checkPoint = firstPos; not needed?
                
                        if (spotType == 1)
                        {
                            checkPoint = firstPos;
                            chainSnippet(firstPos, hotSpotText+5);
                            
                            limitCounter++;
                        } else if (spotType == 2)
                        {
                            checkPoint = firstPos;
                            keyWordSpot(firstPos,hotSpotText+5, startingPos, checkPoint);
                            
                            limitCounter++;
                        } else if (spotType == 3)
                        {
                            checkPoint = firstPos;
                            executeCommand(firstPos, hotSpotText+5);
                            
                            limitCounter++;
                        } else if (spotType == 4)
                        {
                            checkPoint = firstPos;
                            launchMessageBox(firstPos,hotSpotText+5);

                            limitCounter++;
                        } else if (spotType == 5)
                        {
                            checkPoint = firstPos;
                            evaluateHotSpot(firstPos,hotSpotText+5);

                            limitCounter++;
                        } else if (spotType == 6)
                        {
                            checkPoint = firstPos;
                            webRequest(firstPos,hotSpotText+5);

                            limitCounter++;
                        }
                    } else
                    {
                        //alert();
                    }
                }
                else
                {
                    if (!g_hotspotParams.empty())
                    {
                        paramsInsertion(firstPos,hotSpot,checkPoint);
                    } else
                    {
                        normalSpotTriggered = true;
                    }

                    limitCounter++;
                }
            }
        }
        
    } while ((spotComplete>=0) && (spot>0) && (limitCounter<pc.configInt[CHAIN_LIMIT]) && !((g_hotspotParams.empty()) && (normalSpotTriggered))  );  // && (spotType!=0)

    //TODO: loosen the limit to the limit of special spot, and ++limit for every search so that less frezze will happen
    if (limitCounter>=pc.configInt[CHAIN_LIMIT]) showMessageBox(TEXT("Dynamic hotspots triggering limit exceeded."));
    //if (limitCounter>=pc.configInt[CHAIN_LIMIT]) ::MessageBox(nppData._nppHandle, TEXT("Dynamic hotspots triggering limit exceeded."), TEXT(PLUGIN_NAME), MB_OK);
    
    if (limitCounter>0)
    {
        delete [] hotSpot;
        delete [] hotSpotText;

        return true;
    }
    return false;
}

void paramsInsertion(int &firstPos, char* hotSpot, int &checkPoint)
{
    if (!g_hotspotParams.empty())
    {
        //alertString(*g_hotspotParams.begin());
        char* hotspotParamsCharArray = new char [(*g_hotspotParams.begin()).size()+1];
        strcpy(hotspotParamsCharArray, (*g_hotspotParams.begin()).c_str());
        
        g_hotspotParams.erase(g_hotspotParams.begin());
        
        if (strlen(hotspotParamsCharArray)>0)
        {
            
            //::SendScintilla(SCI_SETSEL,firstPos,secondPos+3);
            bool first = true;
            int found;
            do
            {
                ::SendScintilla(SCI_GOTOPOS,firstPos,0);
                found = searchNext(hotSpot);
                //endPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                //::SendScintilla(SCI_SETSEL,endPos-strlen(hotSpot),endPos);
                if (found >=0)
                {
                    if (first)
                    {
                        checkPoint = checkPoint - strlen(hotSpot) +strlen(hotspotParamsCharArray);
                        first = false;
                    }
                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)hotspotParamsCharArray);
                }
                //alertNumber(found);
            } while (found >= 0);
        }

        delete [] hotspotParamsCharArray;
    }
}


void chainSnippet(int &firstPos, char* hotSpotText)
{
    //TODO: there may be a bug here. When the chain snippet contains content with CUT, the firstPos is not updated.
    int triggerPos = strlen(hotSpotText)+firstPos;
    ::SendScintilla(SCI_GOTOPOS,triggerPos,0);
    triggerTag(triggerPos,false, strlen(hotSpotText));
}

////Old implementation of executeCommand
//void executeCommand(int &firstPos, char* hotSpotText)
//{
//    int triggerPos = strlen(hotSpotText)+firstPos;
//    ::SendScintilla(SCI_SETSEL,firstPos,triggerPos);
//    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//    
//    char  psBuffer[130];
//    FILE   *pPipe;
//    int resultLength;
//    //TODO: try the createprocess instead of _popen?
//    //http://msdn.microsoft.com/en-us/library/ms682499(v=vs.85).aspx
//
//    //pPipe = _popen( "ruby -e 'puts 1+1'", "rt" );
//    if( (pPipe = _popen( hotSpotText, "rt" )) == NULL )
//    {    
//        return;
//    }
//
//    ::memset(psBuffer,0,sizeof(psBuffer));
//
//    while(fgets(psBuffer, 129, pPipe))
//    {
//        ::SendScintilla(SCI_REPLACESEL, 128, (LPARAM)psBuffer);
//        ::memset (psBuffer,0,sizeof(psBuffer));
//    }
//    _pclose( pPipe );
//}

void webRequest(int &firstPos, char* hotSpotText)
{
    TCHAR requestType[20];
    int requestTypeLength = 0;

    if (strncmp(hotSpotText,"GET:",4)==0)
    {
        _tcscpy(requestType,TEXT("GET"));
        requestTypeLength = 4;
    } else if (strncmp(hotSpotText,"POST:",5)==0)
    {
        _tcscpy(requestType,TEXT("POST"));
        requestTypeLength = 5;
    } else if (strncmp(hotSpotText,"OPTIONS:",8)==0)
    {
        _tcscpy(requestType,TEXT("OPTIONS"));
        requestTypeLength = 8;
    } else if (strncmp(hotSpotText,"PUT:",4)==0)
    {
        _tcscpy(requestType,TEXT("PUT"));
        requestTypeLength = 5;
    } else if (strncmp(hotSpotText,"HEAD:",5)==0)
    {
        _tcscpy(requestType,TEXT("HEAD"));
        requestTypeLength = 5;
    } else if (strncmp(hotSpotText,"DELETE:",7)==0)
    {
        _tcscpy(requestType,TEXT("DELETE"));
        requestTypeLength = 7;
    } else if (strncmp(hotSpotText,"TRACE:",6)==0)
    {
        _tcscpy(requestType,TEXT("TRACE"));
        requestTypeLength = 6;
    } else if (strncmp(hotSpotText,"CONNECT:",8)==0)
    {
        _tcscpy(requestType,TEXT("CONNECT"));
        requestTypeLength = 8;
    } else
    {
        _tcscpy(requestType,TEXT("GET"));
        requestTypeLength = 0;
    }

    if (requestTypeLength>0)
    {
        SendScintilla(SCI_SETSEL,firstPos,firstPos+requestTypeLength);
        SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
    }

    
    int triggerPos = strlen(hotSpotText)+firstPos-requestTypeLength;
    //TODO: rewrite this part so that it doesn't rely on searchNext, and separate it out to another function to prepare for the implementation of "web snippet import"
    SendScintilla(SCI_GOTOPOS,firstPos,0);
    int spot1 = searchNext("://");
    int serverStart; 
    if ((spot1<0) || (spot1>triggerPos))
    {
        serverStart = firstPos;
    } else
    {
        serverStart = (SendScintilla(SCI_GETCURRENTPOS,0,0))+3;
    }
    SendScintilla(SCI_GOTOPOS,serverStart,0);

    int spot2 = searchNext("/");

    int serverEnd;
    if (spot2<0 || spot1>triggerPos)
    {
        serverEnd = triggerPos;
    } else
    {
        serverEnd = SendScintilla(SCI_GETCURRENTPOS,0,0);
    }

    if (serverEnd - serverStart > 0)
    {

        //char* server = new char[serverEnd-serverStart+1];
        //::SendScintilla(SCI_SETSELECTION,serverStart,serverEnd);
        //::SendScintilla(SCI_GETSELTEXT,0, reinterpret_cast<LPARAM>(server));

        char* server;
        sciGetText(&server,serverStart,serverEnd);

        TCHAR* serverWide = toWideChar(server);

        TCHAR* requestWide = toWideChar(hotSpotText + serverEnd - firstPos);
        
        //alertTCharArray(serverWide);
        //alertTCharArray(requestWide);

        //TODO: customizing type of request
        httpToFile(serverWide,requestWide,requestType,g_currentFocusPath);
        
        delete [] serverWide;
        delete [] requestWide;
        delete [] server;
    }

    ::SendScintilla(SCI_SETSEL,firstPos,triggerPos);
    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
}



void executeCommand(int &firstPos, char* hotSpotText)
{
    //TODO: cater the problem that the path can have spaces..... as shown in the security remarks in http://msdn.microsoft.com/en-us/library/ms682425%28v=vs.85%29.aspx

    int triggerPos = strlen(hotSpotText)+firstPos;
    ::SendScintilla(SCI_SETSEL,firstPos,triggerPos);
    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");

    bool silent = false;
    int offset = 0;
    if (strncmp(hotSpotText,"SILENT:",7)==0)
    {
        silent = true;
        offset = 7;
    }


    HANDLE processStdinRead = NULL;
    HANDLE processStdinWrite = NULL;

    HANDLE processStdoutRead = NULL;
    HANDLE processStdoutWrite = NULL;

    SECURITY_ATTRIBUTES securityAttributes;

    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = true;
    securityAttributes.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&processStdoutRead, &processStdoutWrite, &securityAttributes, 0) )
    {
        //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"->StdoutRd CreatePipe\n");
    }

    if (!SetHandleInformation(processStdoutRead, HANDLE_FLAG_INHERIT, 0) )
    {
        //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"->Stdout SetHandleInformation\n");
    }

    if (!CreatePipe(&processStdinRead, &processStdinWrite, &securityAttributes, 0))
    {
        //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"->Stdin CreatePipe\n");
    }

   if (!SetHandleInformation(processStdinWrite, HANDLE_FLAG_INHERIT, 0) )
   {
       //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"->Stdin SetHandleInformation\n");
   }

    TCHAR* cmdLine = toWideChar(hotSpotText+offset);

    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    bool processSuccess = false;

    ZeroMemory( &pi, sizeof(PROCESS_INFORMATION) );
    ZeroMemory( &si, sizeof(STARTUPINFO) );

    si.cb = sizeof(STARTUPINFO);
    si.hStdError = processStdoutWrite;
    si.hStdOutput = processStdoutWrite;
    si.hStdInput = processStdinRead;
    // Hide window
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;

    // Create process.
    processSuccess = CreateProcess(
        NULL,
        cmdLine,  // command line
        NULL,     // process security attributes
        NULL,    // primary thread security attributes
        true,     // handles are inherited
        0,        // creation flags
        NULL,     // use parent's environment
        NULL,     // use parent's current directory
        &si,      // STARTUPINFO pointer
        &pi       // receives PROCESS_INFORMATION
        );     

    if (!processSuccess)
    {
        //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"->Error in CreateProcess\n");
        char* hotSpotTextCmd = new char[strlen(hotSpotText)+8];
        strcpy(hotSpotTextCmd, "cmd /c ");
        strcat(hotSpotTextCmd,hotSpotText+offset);
        //strcpy(hotSpotTextCmd, hotSpotText);
        TCHAR* cmdLine2 = toWideChar(hotSpotTextCmd);

        processSuccess = CreateProcess(
        NULL,
        cmdLine2,  // command line
        NULL,     // process security attributes
        NULL,    // primary thread security attributes
        true,     // handles are inherited
        0,        // creation flags
        NULL,     // use parent's environment
        NULL,     // use parent's current directory
        &si,      // STARTUPINFO pointer
        &pi       // receives PROCESS_INFORMATION
        );

        if (!processSuccess)
        {
            //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"->Error in CreateProcess\n");
        }
        else
        {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        delete [] cmdLine2;
        delete [] hotSpotTextCmd;
    

    }
    else
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    delete [] cmdLine;
    
    
    //TODO: investigate the possibility to delay reading this part. So the process will keep giving output but I paste it into the editor when I see fit.
    if (!silent)
    {
        const int bufSize = 100;
        DWORD read;
        char buffer[bufSize];
        bool readSuccess = false;

        if (!CloseHandle(processStdoutWrite))
        {
            //::SendMessage(curScintilla, SCI_INSERTTEXT, 0, (LPARAM)"->StdOutWr CloseHandle\n");
        }

        //::Sleep(100);  //this is temporary solution to the incorrectly written output to npp.....

        //HWND curScintilla = getCurrentScintilla();
        while (1)
        {   
            readSuccess = ReadFile(processStdoutRead, buffer, bufSize - 1, &read, NULL);
            
            if (!readSuccess || read == 0 ) break;

            buffer[read] = '\0';
            
            //::SendMessage(curScintilla, SCI_REPLACESEL, bufSize - 1, (LPARAM)Buffer);
            ::SendScintilla(SCI_REPLACESEL, bufSize - 1, (LPARAM)buffer);
            //::SendScintilla(SCI_INSERTTEXT, firstPos, (LPARAM)Buffer);

            if (!readSuccess ) break;
        
        }
    }

   //::SendScintilla(SCI_INSERTTEXT, 0, (LPARAM)"->End of process execution.\n");
}


std::string evaluateCall(char* expression)
{
    //TODO: can use smartsplit for string comparison (need to spply firstPos and TriggerPos instead of hotSpotExt for this function)
    //TODO: refactor the numeric comparison part
    std::vector<std::string> expressions;
    
    expressions = toVectorString(expression,';');
    bool stringComparison = false;

    int i = 0;
    int j = 0;
    for (i = 0;i<expressions.size();i++)
    {
        
        if (expressions[i].length()>1)
        {
            if ((expressions[i][0] != '"') || (expressions[i][expressions[i].length()-1] != '"'))
            {
                // This is math expression
                //Expression y(expressions[i]);
                //if ((y.evaluate(expressions[i])) == 0)
                //{
                if (execDuckEval(expressions[i]) == 0)
                {
                } else 
                {
                    //alertCharArray("error");
                    expressions[i] = "error";
                    stringComparison = true;
                }
            } 
            else
            {
                // This is string
                expressions[i] = expressions[i].substr(1,expressions[i].length()-2);
                stringComparison = true;
            }
        } else if (expressions[i].length()>0)
        {
            // This is math expression
            //Expression y(expressions[i]);
            //if ((y.evaluate(expressions[i])) == 0)
            //{
            
            if (execDuckEval(expressions[i]) == 0)
            {
            } else 
            {
                //alertCharArray("error");
                expressions[i] = "error";
                stringComparison = true;   
            }
        } else
        {
            // This is string
            expressions[i] = "";
            stringComparison = true;
        }
    
    }
    
    std::string evaluateResult = "";
    double compareResult = 0;
    double storedCompareResult = 0;
    
    if (expressions.size() == 1)
    {
        evaluateResult = expressions[0];
    } else
    {
        
        if (stringComparison)
        {
            for (j = 1;j<expressions.size();j++)
            {
                for (i = j;i<expressions.size();i++)
                {
                    compareResult = expressions[i].compare(expressions[j-1]);
                    if (::abs(compareResult) > ::abs(storedCompareResult)) storedCompareResult = compareResult;
                }
            }
        
        } else
        {
            for (j = 1;j<expressions.size();j++)
            {
                for (i = j;i<expressions.size();i++)
                {
                    //std::stringstream ss0(expressions[j-1]);
                    //std::stringstream ss1(expressions[i]);
                    //double d0;
                    //double d1;
                    //ss0 >> d0;
                    //ss1 >> d1;
                    double d0 = toDouble(expressions[j-1]);
                    double d1 = toDouble(expressions[i]);

                    compareResult = d0 - d1;
                    if (::abs(compareResult) > ::abs(storedCompareResult)) storedCompareResult = compareResult;
                }
            }
        }
        
        //std::stringstream ss;
        //ss << ::abs(storedCompareResult);
        //evaluateResult = ss.str();
        evaluateResult = toString((double)::abs(storedCompareResult));
    }
    return evaluateResult;

}

void evaluateHotSpot(int &firstPos, char* hotSpotText)
{
    std::string evaluateResult;
    //TODO: should allow for a more elaborate comparison output
    
    int triggerPos = strlen(hotSpotText)+firstPos;
    SendScintilla(SCI_GOTOPOS,firstPos,0);
    
    int mode = 0;
    char* preParam;
    char delimiter1 = '?';
    char delimiter2 = ':';
    std::string verboseText = " => ";
    int offset=0;
    
    if (strncmp(hotSpotText,"VERBOSE'",8) == 0)
    {
        mode = 1;
        
        ::SendScintilla(SCI_GOTOPOS,firstPos + 8,0);
        int delimitEnd = searchNext("':");
        
        if ((delimitEnd >= 0) && (delimitEnd < firstPos+strlen(hotSpotText)))
        {
        
            ::SendScintilla(SCI_SETSELECTION,firstPos + 8,delimitEnd);
            //optionDelimiter = new char[delimitEnd - (firstPos + 5 + 8) + 1];
            //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(optionDelimiter));
            sciGetText(&preParam, firstPos + 8, delimitEnd);
            verboseText = toString(preParam);
            delete [] preParam;
            ::SendScintilla(SCI_SETSELECTION,firstPos ,delimitEnd + 2);
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
            offset = delimitEnd + 2 - firstPos;
            //secondPos = secondPos - (delimitEnd + 2 - (firstPos + 5));
            
        } 
    } if (strncmp(hotSpotText,"TERNARY'",8) == 0)
    {
        mode = 0;
        
        ::SendScintilla(SCI_GOTOPOS,firstPos + 8,0);
        int delimitEnd = searchNext("':");
        
        if ((delimitEnd >= 0) && (delimitEnd < firstPos+strlen(hotSpotText)))
        {
        
            ::SendScintilla(SCI_SETSELECTION,firstPos + 8,delimitEnd);
            //optionDelimiter = new char[delimitEnd - (firstPos + 5 + 8) + 1];
            //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(optionDelimiter));
            sciGetText(&preParam, firstPos + 8, delimitEnd);
            delimiter1 = preParam[0];
            if (strlen(preParam)>=2)
            {
                delimiter2 = preParam[1];
            } else
            {
                delimiter2 = preParam[0];
            }
            
            delete [] preParam;
            ::SendScintilla(SCI_SETSELECTION,firstPos ,delimitEnd + 2);
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
            offset = delimitEnd + 2 - firstPos;
            //secondPos = secondPos - (delimitEnd + 2 - (firstPos + 5));
            
        } 
    } else if (strncmp(hotSpotText,"TERNARY:",8) == 0)
    {
        ::SendScintilla(SCI_SETSELECTION,firstPos , firstPos+8);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        offset = 8;
        mode = 0;
    } else if (strncmp(hotSpotText,"VERBOSE:",8) == 0)
    {
        ::SendScintilla(SCI_SETSELECTION,firstPos, firstPos+8);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        offset = 8;
        mode = 1;
    }

    std::vector<std::string> firstSplit;

    firstSplit = smartSplit(firstPos,triggerPos-offset,delimiter1,2);

    if (firstSplit.size()<=1)
    {
        // Simple statament
        evaluateResult = evaluateCall(hotSpotText+offset);

    } else
    {
        // possible ternary statement
        std::vector<std::string> secondSplit;
        secondSplit = smartSplit(firstPos + firstSplit[0].length() + 1 ,triggerPos,delimiter2);  
        
        char* expression = toCharArray(firstSplit[0]);
        evaluateResult = evaluateCall(expression);

        //std::stringstream ss(evaluateResult);
        //double d;
        //ss>>d;

        double d = toDouble(evaluateResult);
        
        if (d > secondSplit.size()-1) d = secondSplit.size()-1;
        else if (d < 0) d = 0;
               
        evaluateResult = secondSplit[d];

    }

    ::SendScintilla(SCI_SETSEL,firstPos,triggerPos-offset);
    if (mode == 1)
    {
        std::stringstream ss;
        ss << firstSplit[0] << verboseText << evaluateResult;
        evaluateResult = ss.str();
    } 

    char* result = toCharArray(evaluateResult);
    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)result);
    delete [] result;
}

void launchMessageBox(int &firstPos, char* hotSpotText)
{
    //TODO: need to find a better way to organize different types of messageboxes, there is probably no need to include all of them
    int triggerPos = strlen(hotSpotText)+firstPos;
    ::SendScintilla(SCI_SETSEL,firstPos,triggerPos);

    char* getTerm;
    getTerm = new char[strlen(hotSpotText)];
    strcpy(getTerm,"");
    
    // TODO: probably can just translate the text like "MB_OK" to the corresponding number and send it to the messagebox message directly. In this case people can just follow microsofe documentation.   
    int messageType = MB_OK;
    if (strncmp(hotSpotText,"OK:",3)==0) 
    {
        messageType = MB_OK;
        strcpy(getTerm,hotSpotText+3);
    } else if (strncmp(hotSpotText,"YESNO:",6)==0) 
    {
        messageType = MB_YESNO;
        strcpy(getTerm,hotSpotText+6);
    } else if (strncmp(hotSpotText,"OKCANCEL:",9)==0) 
    {
        messageType = MB_OKCANCEL;
        strcpy(getTerm,hotSpotText+9);
    } else if (strncmp(hotSpotText,"ABORTRETRYIGNORE:",17)==0) 
    {
        messageType = MB_ABORTRETRYIGNORE;
        strcpy(getTerm,hotSpotText+17);
    } else if (strncmp(hotSpotText,"CANCELTRYCONTINUE:",18)==0) 
    {
        messageType = MB_CANCELTRYCONTINUE;
        strcpy(getTerm,hotSpotText+18);
    } else if (strncmp(hotSpotText,"RETRYCANCEL:",12)==0) 
    {
        messageType = MB_RETRYCANCEL;
        strcpy(getTerm,hotSpotText+12);
    } else if (strncmp(hotSpotText,"YESNOCANCEL:",12)==0) 
    {
        messageType = MB_YESNOCANCEL;
        strcpy(getTerm,hotSpotText+12);
    } else
    {
        delete [] getTerm;    //TODO: probably should have some default behaviour here?
        //TODO: Confirm to use return here, it stops the msg box launching when the msgbox type is unknown
        return;
    }

    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
    TCHAR* getTermWide = toWideChar(getTerm);
    int retVal = 0;
    retVal = showMessageBox(getTermWide,messageType);
    //retVal = ::MessageBox(nppData._nppHandle, getTermWide, TEXT(PLUGIN_NAME), messageType);
    char countText[10];
    ::_itoa(retVal, countText, 10);
    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)countText);
    delete [] getTerm;
    delete [] getTermWide;
}


//void textCopyCut(int type, int &firstPos, char* hotSpotText, int &startingPos, int &checkPoint)
//{
//    if (type == 1)   // COPY
//    {
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//        ::SendScintilla(SCI_WORDLEFTEXTEND,0,0);
//        ::SendScintilla(SCI_COPY,0,0);
//    } else if (type == 2)    // CUT
//    {
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//        ::SendScintilla(SCI_WORDLEFTEXTEND,0,0);
//        startingPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
//        if (checkPoint > startingPos) checkPoint = startingPos;
//        if (g_lastTriggerPosition > startingPos) g_lastTriggerPosition = startingPos;
//        ::SendScintilla(SCI_CUT,0,0);
//    } else if (type == 3)     // COPY'
//    {
//        int keywordLength = 5;
//        int hotSpotTextLength = strlen(hotSpotText);
//        int paramNumber = 0;
//        if (hotSpotTextLength - (keywordLength + 1) > 0)
//        {
//            char* param;
//            param = new char[hotSpotTextLength - (keywordLength + 1) + 1];
//            strncpy(param,hotSpotText+keywordLength,hotSpotTextLength - (keywordLength + 1));
//            param[hotSpotTextLength - (keywordLength + 1)] = '\0';
//            paramNumber = ::atoi(param);
//        }
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//
//        if (paramNumber > 0)
//        {
//            
//            int targetLine = (::SendScintilla(SCI_LINEFROMPOSITION,startingPos-1,0)) - paramNumber + 1;
//            if (targetLine<0) targetLine = 0;
//            int targetPos = ::SendScintilla(SCI_POSITIONFROMLINE,targetLine,0);
//            ::SendScintilla(SCI_SETSELECTION, targetPos, startingPos-1);
//            //::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//            //::SendScintilla(SCI_WORDLEFTEXTEND,0,0);
//            ::SendScintilla(SCI_COPY,0,0);
//        
//        }
//    } else if (type == 4)     // CUT'
//    {
//        int keywordLength = 4;
//        int hotSpotTextLength = strlen(hotSpotText);
//        int paramNumber = 0;
//        if (hotSpotTextLength - (keywordLength + 1) > 0)
//        {
//            char* param;
//            param = new char[hotSpotTextLength - (keywordLength + 1) + 1];
//            strncpy(param,hotSpotText+keywordLength,hotSpotTextLength - (keywordLength + 1));
//            param[hotSpotTextLength - (keywordLength + 1)] = '\0';
//            paramNumber = ::atoi(param);
//        }
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//
//        if (paramNumber > 0)
//        {
//            
//            int targetLine = (::SendScintilla(SCI_LINEFROMPOSITION,startingPos-1,0)) - paramNumber + 1;
//            if (targetLine<0) targetLine = 0;
//            int targetPos = ::SendScintilla(SCI_POSITIONFROMLINE,targetLine,0);
//            ::SendScintilla(SCI_SETSELECTION, targetPos, startingPos-1);
//
//            if (checkPoint > startingPos) checkPoint = targetPos;
//            if (g_lastTriggerPosition > startingPos) g_lastTriggerPosition = targetPos;
//            //::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//            //::SendScintilla(SCI_WORDLEFTEXTEND,0,0);
//            ::SendScintilla(SCI_CUT,0,0);
//        }
//    } else if (type == 5)     // COPYLINE
//    {
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//        ::SendScintilla(SCI_HOMEEXTEND,0,0);
//        ::SendScintilla(SCI_COPY,0,0);
//    } else if (type == 6)     // CUTLINE
//    {
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//        ::SendScintilla(SCI_HOMEEXTEND,0,0);
//        startingPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
//        if (checkPoint > startingPos) checkPoint = startingPos;
//        if (g_lastTriggerPosition > startingPos) g_lastTriggerPosition = startingPos;
//        ::SendScintilla(SCI_CUT,0,0);
//    } else if (type == 7)     // COPYDOC
//    {
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//        ::SendScintilla(SCI_DOCUMENTSTARTEXTEND,0,0);
//        ::SendScintilla(SCI_COPY,0,0);
//    } else if (type == 8)     // CUTDOC
//    {
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_SETSEL,startingPos-1,startingPos);
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//        ::SendScintilla(SCI_DOCUMENTSTARTEXTEND,0,0);
//        startingPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
//        if (checkPoint > startingPos) checkPoint = startingPos;  //TODO: no need to check for this?
//        if (g_lastTriggerPosition > startingPos) g_lastTriggerPosition = startingPos;
//        ::SendScintilla(SCI_CUT,0,0);
//    } else if (type == 9)     // COPY:
//    {
//        int hotSpotTextLength = 5;
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        
//        if (startingPos !=0)
//        {
//            char* getTerm;
//            getTerm = new char[strlen(hotSpotText)];
//            strcpy(getTerm,hotSpotText+hotSpotTextLength);
//            int scriptFound = -1;
//            if (strlen(getTerm)>0)
//            {
//                ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//                scriptFound = searchPrev(getTerm);
//            }
//            delete [] getTerm;
//            
//            int selectionEnd = startingPos-1; // -1 because the space before the snippet tag should not be included
//            int selectionStart;
//
//            int scriptStart = 0;
//            if (scriptFound>=0)
//            {
//                scriptStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
//                selectionStart = scriptStart + strlen(hotSpotText) - hotSpotTextLength;
//            } else
//            {
//                selectionStart = 0;
//                scriptStart = 0;
//            }
//            
//            if (selectionEnd>=selectionStart)
//            {
//                ::SendScintilla(SCI_SETSEL,selectionStart,selectionEnd);
//                ::SendScintilla(SCI_COPY,0,0);
//            } else
//            {
//                alertCharArray("keyword COPY: caused an error.");
//            }
//        } else
//        {
//            //TODO: error message when using CUT: at the beginning of the document?
//        }
//    } else if (type == 10)     // CUT:
//    {
//        int hotSpotTextLength = 4;
//        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//        
//        if (startingPos !=0)
//        {
//            char* getTerm;
//            getTerm = new char[strlen(hotSpotText)];
//            strcpy(getTerm,hotSpotText+hotSpotTextLength);
//            int scriptFound = -1;
//            if (strlen(getTerm)>0)
//            {
//                ::SendScintilla(SCI_GOTOPOS,startingPos-1,0);
//                scriptFound = searchPrev(getTerm);
//            }
//            delete [] getTerm;
//            
//            int selectionEnd = startingPos-1; // -1 because the space before the snippet tag should not be included
//            int selectionStart;
//
//            int scriptStart = 0;
//            if (scriptFound>=0)
//            {
//                scriptStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
//                selectionStart = scriptStart+strlen(hotSpotText)-hotSpotTextLength;
//            } else
//            {
//                selectionStart = 0;
//                scriptStart = 0;
//            }
//            
//            if (selectionEnd>=selectionStart)
//            {
//                ::SendScintilla(SCI_SETSEL,selectionStart,selectionEnd);
//                ::SendScintilla(SCI_COPY,0,0);
//                ::SendScintilla(SCI_SETSEL,scriptStart,selectionEnd+1); //+1 to make up the -1 in setting the selection End
//                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
//
//                startingPos = scriptStart;
//                if (checkPoint > startingPos) checkPoint = startingPos;
//                if (g_lastTriggerPosition > startingPos) g_lastTriggerPosition = startingPos;
//
//            } else
//            {
//                alertCharArray("keyword CUT: caused an error.");
//            }
//        } else
//        {
//            //TODO: error message when using CUT: at the beginning of the document?
//        }
//    }
//
//}



void textCopyCut(int sourceType, int operationType, int &firstPos, char* hotSpotText, int &startingPos, int &checkPoint)
{
    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
    if (firstPos != 0)
    {
        int scriptStart;
        int selectionStart;
        int selectionEnd;

        ::SendScintilla(SCI_SETSEL,firstPos-1,firstPos);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        ::SendScintilla(SCI_GOTOPOS,firstPos-1,0);

        if (sourceType == 1)
        {
            ::SendScintilla(SCI_WORDLEFTEXTEND,0,0);

        } else if (sourceType == 2)     
        {
            ::SendScintilla(SCI_HOMEEXTEND,0,0);

        } else if (sourceType == 3)
        {
            ::SendScintilla(SCI_DOCUMENTSTARTEXTEND,0,0);

        } else if (sourceType == 4) 
        {
            int keywordLength;
            if (operationType == 0) keywordLength = 8;
            else if (operationType == 1) keywordLength = 9;
            else if (operationType == 2) keywordLength = 10;
            else if (operationType == 3) keywordLength = 11;

            int paramNumber = 0;

            char* getTerm;
            getTerm = new char[strlen(hotSpotText)];
            strcpy(getTerm,hotSpotText+keywordLength);
            
            paramNumber = ::atoi(getTerm);

            delete [] getTerm;

            if (paramNumber > 0) 
            {
            } else 
            {
                paramNumber = 1;
            }
            
            int targetLine = (::SendScintilla(SCI_LINEFROMPOSITION,firstPos-1,0)) - paramNumber + 1;
            if (targetLine<0) targetLine = 0;
            int targetPos = ::SendScintilla(SCI_POSITIONFROMLINE,targetLine,0);
            ::SendScintilla(SCI_SETSELECTION, targetPos, firstPos-1);


        } else if (sourceType == 5)
        {
            int keywordLength;
            if (operationType == 0) keywordLength = 4;
            else if (operationType == 1) keywordLength = 5;
            else if (operationType == 2) keywordLength = 6;
            else if (operationType == 3) keywordLength = 7;

            char* getTerm;
            getTerm = new char[strlen(hotSpotText)];
            strcpy(getTerm,hotSpotText+keywordLength);

            int scriptFound = -1;
            if (strlen(getTerm)>0) scriptFound = searchPrev(getTerm);

            delete [] getTerm;
            
            selectionEnd = firstPos-1; // -1 because the space before the snippet tag should not be included

            if (scriptFound>=0)
            {
                scriptStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                selectionStart = scriptStart + strlen(hotSpotText) - keywordLength;
                if (selectionEnd < selectionStart) selectionStart = selectionEnd;
                ::SendScintilla(SCI_SETSEL,selectionStart,selectionEnd);
            } else
            {

                ::SendScintilla(SCI_WORDLEFTEXTEND,0,0);
                scriptStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                selectionStart = scriptStart;
            }

            
        }

        if (operationType == 0)
        {
            startingPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
            if (sourceType == 5) startingPos = scriptStart;
            if (checkPoint > startingPos) checkPoint = startingPos;  
            if (g_lastTriggerPosition > startingPos) g_lastTriggerPosition = startingPos;
            ::SendScintilla(SCI_CUT,0,0);
            if (sourceType == 5)
            {
                ::SendScintilla(SCI_SETSEL,scriptStart,selectionStart); //Delete the search key
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
            }
        } else if (operationType == 1)  
        {
            ::SendScintilla(SCI_COPY,0,0);
            
        } else if (operationType == 2)
        {
            startingPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
            if (sourceType == 5) startingPos = scriptStart;
            if (checkPoint > startingPos) checkPoint = startingPos;
            if (g_lastTriggerPosition > startingPos) g_lastTriggerPosition = startingPos;
            //delete [] g_customClipBoard;
            //g_customClipBoard = new char [(::SendScintilla(SCI_GETSELECTIONEND,0,0)) - (::SendScintilla(SCI_GETSELECTIONSTART,0,0)) +1];
            //::SendScintilla(SCI_GETSELTEXT,0, reinterpret_cast<LPARAM>(g_customClipBoard));
            char* tempCustomClipBoardText;
            sciGetText(&tempCustomClipBoardText,(::SendScintilla(SCI_GETSELECTIONSTART,0,0)),(::SendScintilla(SCI_GETSELECTIONEND,0,0)));
            g_customClipBoard = toString(tempCustomClipBoardText);
            delete [] tempCustomClipBoardText;
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
            if (sourceType == 5)
            {
                ::SendScintilla(SCI_SETSEL,scriptStart,selectionStart); //Delete the search key
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
            }

        } else if (operationType == 3)
        {
            char* tempCustomClipBoardText;
            sciGetText(&tempCustomClipBoardText,(::SendScintilla(SCI_GETSELECTIONSTART,0,0)),(::SendScintilla(SCI_GETSELECTIONEND,0,0)));
            g_customClipBoard = toString(tempCustomClipBoardText);
            delete [] tempCustomClipBoardText;
        }
    }

}


void keyWordSpot(int &firstPos, char* hotSpotText, int &startingPos, int &checkPoint)
{
    int hotSpotTextLength = strlen(hotSpotText);
    int triggerPos = hotSpotTextLength+firstPos;

    ::SendScintilla(SCI_SETSEL,firstPos,triggerPos);
    //TODO: At least I should rearrange the keyword a little bit for efficiency
    //TODO: refactor the logic of checking colon version, for example DATE and DATE: for efficiency
    //TODO: remove the GET series
    //TODO: all keywords should have a "no colon" version and show error message of "params needed"

    if (strcmp(hotSpotText,"PASTE") == 0)
    {
        ::SendScintilla(SCI_PASTE,0,0);
	    
    } else if (strcmp(hotSpotText,"FTPASTE") == 0)
    {
        char* tempCustomClipBoardText = toCharArray(g_customClipBoard);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)tempCustomClipBoardText);
        delete [] tempCustomClipBoardText;
    } else if ((strcmp(hotSpotText,"CUT") == 0) || (strcmp(hotSpotText,"CUTWORD") == 0))
    {
        textCopyCut(1, 0, firstPos, hotSpotText, startingPos, checkPoint); 
    } else if ((strcmp(hotSpotText,"COPY") == 0) || (strcmp(hotSpotText,"COPYWORD") == 0) || (strcmp(hotSpotText,"GET") == 0))
    {
        textCopyCut(1, 1, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strcmp(hotSpotText,"CUTLINE") == 0)
    {
        textCopyCut(2, 0, firstPos, hotSpotText, startingPos, checkPoint);
    } else if ((strcmp(hotSpotText,"COPYLINE") == 0) || (strcmp(hotSpotText,"GETLINE") == 0))
    {
        textCopyCut(2, 1, firstPos, hotSpotText, startingPos, checkPoint);
    } else if ((strcmp(hotSpotText,"CUTDOC") == 0) || (strcmp(hotSpotText,"CUTALL") == 0))
    {
        textCopyCut(3, 0, firstPos, hotSpotText, startingPos, checkPoint);
    } else if ((strcmp(hotSpotText,"COPYDOC") == 0) || (strcmp(hotSpotText,"GETALL") == 0))
    {
        textCopyCut(3, 1, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"CUTLINE:",8) == 0)
    {
        textCopyCut(4, 0, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"COPYLINE:",9) == 0)
    {
        textCopyCut(4, 1, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"CUT:",4) == 0) 
    {
        textCopyCut(5, 0, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"COPY:",5) == 0) 
    {
        textCopyCut(5, 1, firstPos, hotSpotText, startingPos, checkPoint);
    } else if ((strcmp(hotSpotText,"FTCUT") == 0) || (strcmp(hotSpotText,"FTCUTWORD") == 0))
    {
        textCopyCut(1, 2, firstPos, hotSpotText, startingPos, checkPoint); 
    } else if ((strcmp(hotSpotText,"FTCOPY") == 0) || (strcmp(hotSpotText,"FTCOPYWORD") == 0))
    {
        textCopyCut(1, 3, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strcmp(hotSpotText,"FTCUTLINE") == 0)
    {
        textCopyCut(2, 2, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strcmp(hotSpotText,"FTCOPYLINE") == 0)
    {
        textCopyCut(2, 3, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strcmp(hotSpotText,"FTCUTDOC") == 0)
    {
        textCopyCut(3, 2, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strcmp(hotSpotText,"FTCOPYDOC") == 0)
    {
        textCopyCut(3, 3, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"FTCUTLINE:",10) == 0)
    {
        textCopyCut(4, 2, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"FTCOPYLINE:",11) == 0)
    {
        textCopyCut(4, 3, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"FTCUT:",6) == 0) 
    {
        textCopyCut(5, 2, firstPos, hotSpotText, startingPos, checkPoint);
    } else if (strncmp(hotSpotText,"FTCOPY:",7) == 0) 
    {
        textCopyCut(5, 3, firstPos, hotSpotText, startingPos, checkPoint);
    } else if ((strcmp(hotSpotText,"TEMP") == 0) || (strcmp(hotSpotText,"TEMPFILE") == 0))
    {
        insertPath(g_fttempPath);

    } else if (strcmp(hotSpotText,"FILEFOCUS") == 0)
    {
        insertPath(g_currentFocusPath);

    } else if (strncmp(hotSpotText,"SETFILE:",8) == 0)  //TODO: a lot of refactoring needed for the file series
    {
        char* getTerm;
        
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+8);
        TCHAR* getTermWide = toWideChar(getTerm);
        if (strlen(getTerm) < MAX_PATH)
        {
            ::_tcscpy_s(g_currentFocusPath,getTermWide);
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
            emptyFile(g_currentFocusPath);
        } else 
        {
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"The file name is too long.");
        }

        delete [] getTerm;
        delete [] getTermWide;
        
    } else if (strncmp(hotSpotText,"WRITE:",6) == 0)  
    {
        //TODO: Should have Append mode  
        //TODO: refactor file wriiting to another function
        emptyFile(g_currentFocusPath);
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+6);
        std::ofstream fileStream(g_currentFocusPath, std::ios::binary); // need to open in binary so that there will not be extra spaces written to the document
        if (fileStream.is_open())
        {
            fileStream << getTerm;
            fileStream.close();
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        } else
        {
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"The file cannot be opened.");
        }
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"WRITETEMP:",10) == 0)
    {
        emptyFile(g_fttempPath);
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+10);
        std::ofstream fileStream(g_fttempPath, std::ios::binary); // need to open in binary so that there will not be extra spaces written to the document
        if (fileStream.is_open())
        {
            fileStream << getTerm;
            fileStream.close();
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        } else
        {
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"The file cannot be opened.");
        }
        delete [] getTerm;
    } else if (strcmp(hotSpotText,"FTWRITE") == 0)
    {
        emptyFile(g_currentFocusPath);
        
        std::ofstream fileStream(g_currentFocusPath, std::ios::binary); // need to open in binary so that there will not be extra spaces written to the document
        if (fileStream.is_open())
        {
            fileStream << g_customClipBoard;
            fileStream.close();
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        } else
        {
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"The file cannot be opened.");
        }

    } else if (strcmp(hotSpotText,"FTWRITETEMP") == 0)
    {
        emptyFile(g_fttempPath);
        
        std::ofstream fileStream(g_fttempPath, std::ios::binary); // need to open in binary so that there will not be extra spaces written to the document
        if (fileStream.is_open())
        {
            fileStream << g_customClipBoard;
            fileStream.close();
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        } else
        {
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"The file cannot be opened.");
        }

    } else if (strcmp(hotSpotText,"READ") == 0)
    {
        char* buffer;

        std::ifstream fileStream;
        fileStream.open(g_currentFocusPath, std::ios::binary | std::ios::in);

        if (fileStream.is_open())
        {
            fileStream.seekg (0, std::ios::end);
            int length = fileStream.tellg();
            fileStream.seekg (0, std::ios::beg);

            buffer = new char [length+1];
            
            fileStream.read (buffer,length);
            buffer[length] = '\0';
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)buffer);
            fileStream.close();
            delete[] buffer;
        } else
        {
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"The file cannot be opened.");
        }
  
    } else if (strcmp(hotSpotText,"READTEMP") == 0)
    {
        char* buffer;
        
        std::ifstream fileStream;
        fileStream.open(g_fttempPath, std::ios::binary | std::ios::in);
        if (fileStream.is_open())
        {
            fileStream.seekg (0, std::ios::end);
            int length = fileStream.tellg();
            fileStream.seekg (0, std::ios::beg);

            buffer = new char [length+1];
            
            fileStream.read (buffer,length);
            buffer[length] = '\0';
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)buffer);
            fileStream.close();
            delete[] buffer;
        } else
        {
            ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"The file cannot be opened.");
        }
    } else if (strncmp(hotSpotText,"UPPER:",6)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+6);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)::_strupr(getTerm));
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"LOWER:",6)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+6);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)::_strlwr(getTerm));
        delete [] getTerm;
    } else if (strcmp(hotSpotText,"WINFOCUS")==0)
    {
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        setFocusToWindow();
    } else if (strcmp(hotSpotText,"SETWIN")==0)
    {
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        ::searchWindowByName("");
        
    } else if (strncmp(hotSpotText,"SETWIN:",7)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+7);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        ::std::string tempString(getTerm);
        ::searchWindowByName(tempString);
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"SETCHILD:",9)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+9);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        ::std::string tempString(getTerm);
        ::searchWindowByName(tempString,g_tempWindowHandle);
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"KEYDOWN:",8)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+8);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        setFocusToWindow();
        generateKey(toVk(getTerm),true);
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"KEYUP:",6)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+6);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        setFocusToWindow();
        generateKey(toVk(getTerm),false);
        delete [] getTerm;
    }  else if (strncmp(hotSpotText,"KEYHIT:",7)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+7);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        setFocusToWindow();
        generateKey(toVk(getTerm),true);
        generateKey(toVk(getTerm),false);
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"SCOPE:",6)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+6);
        //TCHAR* scopeWide = new TCHAR[strlen(getTerm)*4+!];
        pc.configText[CUSTOM_SCOPE] = toWideChar(getTerm);  //TODO: memory leak here?
        pc.callWriteConfigText(CUSTOM_SCOPE);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        updateDockItems(false,false);
        //snippetHintUpdate();
        delete [] getTerm;
    } else if (strcmp(hotSpotText,"DATE")==0)
    {
        char* dateText = getDateTime(NULL,true,DATE_LONGDATE);
        ::SendScintilla( SCI_REPLACESEL, 0, (LPARAM)dateText);
        delete [] dateText;
        //insertDateTime(true,DATE_LONGDATE,curScintilla);
        
    } else if (strcmp(hotSpotText,"TIME")==0)
    {
        char* timeText = getDateTime(NULL,false,0);
        ::SendScintilla( SCI_REPLACESEL, 0, (LPARAM)timeText);
        delete [] timeText;
        //insertDateTime(false,0,curScintilla);
    } else if (strncmp(hotSpotText,"DATE:",5)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+5);
        char* dateReturn = getDateTime(getTerm);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)dateReturn);
        delete [] dateReturn;
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"TIME:",5)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+5);
        char* timeReturn = getDateTime(getTerm,false);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)timeReturn);
        delete [] timeReturn;
        delete [] getTerm;
    } else if (strncmp(hotSpotText,"REPLACE:",8)==0)
    {
        //TODO: cater backward search (need to investigate how to set the checkpoint, firstpos and starting pos
        //TODO: allow custom separator
        std::vector<std::string> params = smartSplit(firstPos + 8, triggerPos,',');
        SendScintilla(SCI_SETSEL,firstPos,triggerPos);
        SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        if (params.size() > 1)
        {
            searchAndReplace(params[0],params[1]);
        } else
        {
            //TODO: error message of not enough params
        }
        
    }  else if (strncmp(hotSpotText,"REGEXREPLACE:",13)==0)
    {
        std::vector<std::string> params = smartSplit(firstPos + 13, triggerPos,',');
        SendScintilla(SCI_SETSEL,firstPos,triggerPos);
        SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        if (params.size() > 1)
        {
            searchAndReplace(params[0],params[1],true);
        } else
        {
            //TODO: error message of not enough params
        }
        
    } else if (strcmp(hotSpotText,"FILENAME")==0)
    {
        insertNppPath(NPPM_GETNAMEPART);
        
    } else if (strcmp(hotSpotText,"EXTNAME")==0)
    {
        insertNppPath(NPPM_GETEXTPART);
        
    } else if (strcmp(hotSpotText,"DIRECTORY")==0)
    {
        insertNppPath(NPPM_GETCURRENTDIRECTORY);
    } else if (strncmp(hotSpotText,"SLEEP:",6)==0)
    {
        char* getTerm;
        getTerm = new char[hotSpotTextLength];
        strcpy(getTerm,hotSpotText+6);
        
        int sleepLength = ::atoi(getTerm);
        ::Sleep(sleepLength);
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
        delete [] getTerm;
        
    } else if ((strncmp(hotSpotText,"CLEAN_",6)==0) && (strncmp(hotSpotText+7,":",1)==0))
    {
        // TODO: fill in content for this CLEAN keyword
        
    } else if ((strncmp(hotSpotText,"COUNT_",6)==0) && (strncmp(hotSpotText+7,":",1)==0))
    {
        // TODO: fill in content for this COUNT keyword
    } else
    {
        char* errorMessage = new char[hotSpotTextLength+40];
        if (hotSpotTextLength>0)
        {
            strcpy(errorMessage,"'");
            strcat(errorMessage, hotSpotText);
            strcat(errorMessage,"' is not a keyword in fingertext.");
        } else
        {
            strcpy(errorMessage,"Keyword missing.");
        }
        
        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)errorMessage);
        delete [] errorMessage;
    }

    //else if (strcmp(hotSpotText,"DATESHORT")==0)
    //{
    //    char* dateText = getDateTime(NULL,true,DATE_SHORTDATE);
    //    ::SendScintilla( SCI_REPLACESEL, 0, (LPARAM)dateText);
    //    delete [] dateText;
    //    //insertDateTime(true,DATE_SHORTDATE,curScintilla);
	//    
    //} 
    //else if (strcmp(hotSpotText,"TIMESHORT")==0)
    //{
    //    char* timeText = getDateTime(NULL,false,TIME_NOSECONDS);
    //    ::SendScintilla( SCI_REPLACESEL, 0, (LPARAM)timeText);
    //    delete [] timeText;
    //    //insertDateTime(false,TIME_NOSECONDS,curScintilla);
    //    
    //} 
}

void searchAndReplace(std::string key, std::string text, bool regexp)
{
    
    char* searchKey = new char[key.length()+1];
    char* replaceText = new char[text.length()+1];
    strcpy(searchKey,key.c_str());
    strcpy(replaceText,text.c_str());
    int keySpot = -1;
    keySpot = searchNext(searchKey,regexp);

    while (keySpot >= 0)
    {
        if (keySpot>=0)
        {
            SendScintilla(SCI_REPLACESEL,0,(LPARAM)replaceText);
        }
        keySpot = searchNext(searchKey,regexp);
    }
}

//TODO: insertpath and insertnpppath (and/or other insert function) need refactoring
void insertPath(TCHAR* path)
{
    char* pathText = toCharArray(path,(int)::SendScintilla(SCI_GETCODEPAGE, 0, 0));
	//WideCharToMultiByte((int)::SendScintilla(SCI_GETCODEPAGE, 0, 0), 0, path, -1, pathText, MAX_PATH, NULL, NULL);
    ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)pathText);
    delete [] pathText;
}

void insertNppPath(int msg)
{
	TCHAR path[MAX_PATH];
	::SendMessage(nppData._nppHandle, msg, 0, (LPARAM)path);

    char* pathText = toCharArray(path,(int)::SendScintilla(SCI_GETCODEPAGE, 0, 0));
	//char pathText[MAX_PATH];
	//WideCharToMultiByte((int)::SendScintilla(SCI_GETCODEPAGE, 0, 0), 0, path, -1, pathText, MAX_PATH, NULL, NULL);
	::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)pathText);

    delete [] pathText;
}

//void insertDateTime(bool date,int type, HWND &curScintilla)
//{
//    TCHAR time[128];
//    SYSTEMTIME formatTime;
//	::GetLocalTime(&formatTime);
//    if (date)
//    {
//        ::GetDateFormat(LOCALE_USER_DEFAULT, type, &formatTime, NULL, time, 128);
//    } else
//    {
//        ::GetTimeFormat(LOCALE_USER_DEFAULT, type, &formatTime, NULL, time, 128);
//    }
//	
//	char timeText[MAX_PATH];
//	WideCharToMultiByte((int)::SendMessage(curScintilla, SCI_GETCODEPAGE, 0, 0), 0, time, -1, timeText, MAX_PATH, NULL, NULL);
//	::SendMessage(curScintilla, SCI_REPLACESEL, 0, (LPARAM)timeText);
//}

char* getDateTime(char *format, bool getDate, int flags)
{
    //HWND curScintilla = getCurrentScintilla();

    TCHAR result[128];
    SYSTEMTIME formatTime;
	::GetLocalTime(&formatTime);
    
    wchar_t* formatWide;
    if (format)
    {
        formatWide = toWideChar(format);
    } else
    {
        formatWide = NULL;
    }
    
    if (getDate)
    {
        ::GetDateFormat(LOCALE_USER_DEFAULT, flags, &formatTime, formatWide, result, 128);
    } else
    {
        ::GetTimeFormat(LOCALE_USER_DEFAULT, flags, &formatTime, formatWide, result, 128);
    }
    
    if (format) delete [] formatWide;
    
    char* resultText = toCharArray(result,(int)::SendScintilla(SCI_GETCODEPAGE, 0, 0));
	//WideCharToMultiByte((int)::SendMessage(curScintilla, SCI_GETCODEPAGE, 0, 0), 0, result, -1, resultText, MAX_PATH, NULL, NULL);
    return resultText;
}

//void goToWarmSpot()
//{
//    HWND curScintilla = getCurrentScintilla();
//    if (!warmSpotNavigation(curScintilla))
//    {
//        ::SendMessage(curScintilla,SCI_BACKTAB,0,0);	
//    }
//
//}
//
//bool warmSpotNavigation(HWND &curScintilla)
//{
//    
//    bool spotFound = searchNext(curScintilla,"${!{}!}");
//    ::SendMessage(curScintilla, SCI_REPLACESEL, 0, (LPARAM)"");
//    return spotFound;
//}



int hotSpotNavigation(char* tagSign, char* tagTail)
{
    int retVal = 0;
    // TODO: consolidate this part with dynamic hotspots? 

    //char tagSign[] = "$[![";
    //int tagSignLength = strlen(tagSign);
    //char tagTail[] = "]!]";
    //int tagTailLength = strlen(tagTail);
    int tagSignLength = 4;

    char *hotSpotText;
    char *hotSpot;

    int tagSpot = searchNext(tagTail);    // Find the tail first so that nested snippets are triggered correctly
	if (tagSpot >= 0)
	{
        if (pc.configInt[PRESERVE_STEPS] == 0) ::SendScintilla(SCI_BEGINUNDOACTION, 0, 0);

        if (searchPrev(tagSign) >= 0)
        {
            int firstPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
            int secondPos = 0;
            grabHotSpotContent(&hotSpotText, &hotSpot, firstPos, secondPos, tagSignLength, tagTail);

            if (strncmp(hotSpotText,"(lis)",5) == 0)
            {
                ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)"");

                ::SendScintilla(SCI_GOTOPOS,firstPos,0);
                
                ::SendScintilla(SCI_AUTOCSETSEPARATOR, (LPARAM)'|', 0); 
                ::SendScintilla(SCI_AUTOCSHOW, 0, (LPARAM)(hotSpotText+5));
                retVal = 3;

            } else if (strncmp(hotSpotText,"(opt)",5) == 0)
            {
                
                ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)hotSpotText);
                ::SendScintilla(SCI_GOTOPOS,firstPos,0);
                int triggerPos = firstPos + strlen(hotSpotText);
                ::SendScintilla(SCI_SETSELECTION,firstPos,firstPos+5);
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
                triggerPos -= 5;

                //TODO: refactor the option hotspot part to a function
                int mode = 0;
                char* preParam;
                char delimiter = '|';
                int offset=0;
                
                if (strncmp(hotSpotText+5,"DELIMIT'",8) == 0)   // TODO: the +5 is not necessary, should delete the (opt) first......
                {
                    mode = 0;
                    ::SendScintilla(SCI_GOTOPOS,firstPos + 8,0);
                    int delimitEnd = searchNext("':");
                    
                    if ((delimitEnd >= 0) && (delimitEnd < triggerPos))
                    {

                        ::SendScintilla(SCI_SETSELECTION,firstPos + 8,delimitEnd);
                        //optionDelimiter = new char[delimitEnd - (firstPos + 5 + 8) + 1];
                        //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(optionDelimiter));
                        sciGetText(&preParam, firstPos + 8, delimitEnd);
                        delimiter = preParam[0];
                        
                        delete [] preParam;
                        ::SendScintilla(SCI_SETSELECTION,firstPos,delimitEnd + 2);
                        ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
                        offset = delimitEnd + 2 - firstPos;
                        //secondPos = secondPos - (delimitEnd + 2 - (firstPos + 5));
                    } 
                } else if (strncmp(hotSpotText+5,"DELIMIT:",8) == 0)
                {
                    mode = 0;
                    ::SendScintilla(SCI_SETSELECTION,firstPos, firstPos + 8);  
                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
                    offset = 8;
                } else if (strncmp(hotSpotText+5,"RANGE:",6) == 0)
                {
                    //TODO: may allow for customizable delimiter for RANGE. but it shouldnt be necessary
                    mode = 1;
                    ::SendScintilla(SCI_SETSELECTION,firstPos, firstPos + 6);  
                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
                    offset = 6;
                } 
                
                cleanOptionItem();

                if (mode == 1)
                {
                    char* getTerm;
                    sciGetText(&getTerm, firstPos,triggerPos-offset);
                    getTerm = quickStrip(getTerm,' ');
                    getTerm = quickStrip(getTerm,'\r');
                    getTerm = quickStrip(getTerm,'\n');
                    getTerm = quickStrip(getTerm,'\t');
                    std::vector<std::string> rangeString = toVectorString(getTerm,'-');
                    delete [] getTerm;

                    int numLength = 1;
                    long rangeStart;
                    long rangeEnd;
                    
                    if (rangeString.size()>1)
                    {   
                        if (rangeString[0].length()<=0) rangeString[0] = "0";
                        if (rangeString[1].length()<=0) rangeString[1] = "0";
                        
                        numLength = rangeString[0].length();
                        if (rangeString[1].length()<rangeString[0].length()) numLength = rangeString[1].length();
                        
                        rangeStart = toLong(rangeString[0]);
                        rangeEnd = toLong(rangeString[1]);
                        //std::stringstream ss1(rangeString[0]);
                        //ss1 >> rangeStart;
                        //
                        //std::stringstream ss2(rangeString[1]);
                        //ss2 >> rangeEnd;
                        //
                        rangeStart = abs(rangeStart);
                        rangeEnd = abs(rangeEnd);

                        int length;
                        if (rangeEnd>=rangeStart)
                        {
                            for (int i = rangeStart; i<=rangeEnd; i++)
                            {
                                std::string s = toString(i);
                                //std::stringstream ss;
                                //ss << i;
                                //std::string s = ss.str();
                                
                                length = s.length();
	                            for (int j = 0; j < numLength - length; j++) s = "0" + s;
                                g_optionArray.push_back(s);
                            }
                        } else
                        {
                            for (int i = rangeStart; i>=rangeEnd; i--)
                            {
                                std::string s = toString(i);
                                //std::stringstream ss;
                                //ss << i;
                                //std::string s = ss.str();

                                length = s.length();
	                            for (int j = 0; j < numLength - length; j++) s = "0" + s;
                                g_optionArray.push_back(s);
                            
                            }
                        }
                    } else
                    {
                        g_optionArray.push_back(rangeString[0]);
                    }
                } else
                {
                    g_optionArray = smartSplit(firstPos,triggerPos-offset,delimiter);
                }
                //g_optionNumber = g_optionNumber + g_optionArray.size();
                
                //tempOptionEnd = firstPos + 5 - strlen(optionDelimiter);
                //int i =0;
                //int optionFound = -1;
                ////while (i<g_optionArrayLength)
                //while(1)
                //{
                //    tempOptionStart = tempOptionEnd + strlen(optionDelimiter);
                //    ::SendScintilla(SCI_GOTOPOS,tempOptionStart,0);
                //    optionFound = searchNext(optionDelimiter);
                //    if ((optionFound>=0) && (::SendScintilla(SCI_GETCURRENTPOS,0,0)<secondPos))
                //    {
                //        tempOptionEnd = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                //        ::SendScintilla(SCI_SETSELECTION,tempOptionStart,tempOptionEnd);
                //        char* optionText;
                //        //optionText = new char[tempOptionEnd - tempOptionStart + 1];
                //        //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(optionText));
                //        sciGetText(&optionText, tempOptionStart, tempOptionEnd);
                //        addOptionItem(optionText);
                //        i++;
                //    } else
                //    {
                //        tempOptionEnd = secondPos-4;
                //        ::SendScintilla(SCI_SETSELECTION,tempOptionStart,tempOptionEnd);
                //        char* optionText;
                //        //optionText = new char[tempOptionEnd - tempOptionStart + 1];
                //        //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(optionText));
                //        sciGetText(&optionText, tempOptionStart, tempOptionEnd);
                //        addOptionItem(optionText);
                //        i++;
                //        
                //        break;
                //    }
                //};


                //g_optionOperating = true; 

                ::SendScintilla(SCI_SETSELECTION,firstPos,triggerPos-offset);
                char* option = toCharArray(g_optionArray[g_optionCurrent]);
                ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)option);
                //g_optionOperating = false; 
                delete [] option;
                ::SendScintilla(SCI_GOTOPOS,firstPos,0);
                g_optionStartPosition = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                g_optionEndPosition = g_optionStartPosition + g_optionArray[g_optionCurrent].length();
                ::SendScintilla(SCI_SETSELECTION,g_optionStartPosition,g_optionEndPosition);
                //alertNumber(g_optionArray.size());
                if (g_optionArray.size() > 1)
                {
                    turnOnOptionMode();
                    //g_optionMode = true;
                    updateOptionCurrent(true);
                }
                //alertNumber(::SendMessage(curScintilla,SCI_GETCURRENTPOS,0,0));
                
                //char* tempText;
                //tempText = new char[secondPos - firstPos + 1];
                //::SendMessage(curScintilla, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(tempText));
                //alertCharArray(tempText);

                //::SendMessage(curScintilla,SCI_SETSELECTION,firstPos,secondPos);
                //::SendMessage(curScintilla, SCI_REPLACESEL, 0, (LPARAM)g_optionArray[g_optionCurrent]);
                retVal = 2;
            } else
            {
                ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)hotSpotText);

                ::SendScintilla(SCI_GOTOPOS,firstPos,0);
                int hotSpotFound=-1;
                int tempPos[100];
                int i=1;
                //TODO: consider refactor this part to another function
                for (i=1;i<=98;i++)
                {
                    tempPos[i]=-1;
                
                    hotSpotFound = searchNext(hotSpot);
                    if ((hotSpotFound>=0) && strlen(hotSpotText)>0)
                    {
                        tempPos[i] = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                        ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)hotSpotText);
                        ::SendScintilla(SCI_GOTOPOS,tempPos[i],0);
                    } else
                    {
                        break;
                        //tempPos[i]=-1;
                    }
                }
                //::SendMessage(curScintilla,SCI_GOTOPOS,::SendMessage(curScintilla,SCI_POSITIONFROMLINE,posLine,0),0);
                //::SendMessage(curScintilla,SCI_GOTOLINE,posLine,0);

                ::SendScintilla(SCI_GOTOPOS,firstPos,0);
                //::SendScintilla(SCI_SCROLLCARET,0,0);
                
                
                ::SendScintilla(SCI_SETSELECTION,firstPos,secondPos-tagSignLength);
                for (int j=1;j<i;j++)
                {
                    if (tempPos[j]!=-1)
                    {
                        ::SendScintilla(SCI_ADDSELECTION,tempPos[j],tempPos[j]+(secondPos-tagSignLength-firstPos));
                    }
                }
                ::SendScintilla(SCI_SETMAINSELECTION,0,0);
                ::SendScintilla(SCI_SCROLLCARET,0,0);
                //::SendScintilla(SCI_LINESCROLL,0,20);
                //TODO: scrollcaret is not working correctly when thre is a dock visible
                retVal = 1;
            }

            delete [] hotSpot;
            delete [] hotSpotText;

            if (pc.configInt[PRESERVE_STEPS]==0) ::SendScintilla(SCI_ENDUNDOACTION, 0, 0);
            
        }
	} else
    {
        //delete [] hotSpot;  // Don't try to delete if it has not been initialized
        //delete [] hotSpotText;
        retVal = 0;
    }
    return retVal;
}

int grabHotSpotContent(char **hotSpotText,char **hotSpot, int firstPos, int &secondPos, int signLength, char* tagTail)
{
    int spotType = 0;

    searchNext(tagTail);
	secondPos = ::SendScintilla(SCI_GETCURRENTPOS,0,0);

    //::SendScintilla(SCI_SETSELECTION,firstPos+signLength,secondPos);
    //*hotSpotText = new char[secondPos - (firstPos + signLength) + 1];
    //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(*hotSpotText));
    sciGetText(&*hotSpotText, (firstPos + signLength), secondPos);

    if (strncmp(*hotSpotText,"(cha)",5)==0)
    {
        spotType = 1;
    } else if (strncmp(*hotSpotText,"(key)",5)==0)
    {
        spotType = 2;
    } else if ((strncmp(*hotSpotText,"(run)",5)==0) || (strncmp(*hotSpotText,"(cmd)",5)==0))  //TODO: the command hotspot is renamed to (run) this line is for keeping backward compatibility
    {
        spotType = 3;
    } else if (strncmp(*hotSpotText,"(msg)",5)==0) //TODO: should think more about this hotspot, It can be made into a more general (ask) hotspot....so keep this feature private for the moment
    {
        spotType = 4;
    } else if (strncmp(*hotSpotText,"(eva)",5)==0)
    {
        spotType = 5;
    } else if ((strncmp(*hotSpotText,"(web)",5)==0) || (strncmp(*hotSpotText,"(www)",5)==0))
    {
        spotType = 6;
    }

    //::SendScintilla(SCI_SETSELECTION,firstPos,secondPos+3);
    //*hotSpot = new char[secondPos+3 - firstPos + 1];
    //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(*hotSpot));

    sciGetText(&*hotSpot,firstPos,secondPos+3);
    ::SendScintilla(SCI_SETSELECTION,firstPos,secondPos+3);
    return spotType;
    //return secondPos;  
}

void showPreview(bool top)
{
    
    TCHAR* bufferWide;
    if (top)
    {
        snippetDock.getSelectText(bufferWide,0);
    } else
    {
        snippetDock.getSelectText(bufferWide);
    }

    if (::_tcslen(bufferWide)>0)
    {

        char* buffer = toCharArray(bufferWide);
        buffer = quickStrip(buffer, ' ');

        int scopeLength = ::strchr(buffer,'>') - buffer - 1;
        int triggerTextLength = strlen(buffer)-scopeLength - 2;
        char* tempTriggerText = new char [ triggerTextLength+1];
        char* tempScope = new char[scopeLength+1];
        
        strncpy(tempScope,buffer+1,scopeLength);
        tempScope[scopeLength] = '\0';
        strncpy(tempTriggerText,buffer+1+scopeLength+1,triggerTextLength);
        tempTriggerText[triggerTextLength] = '\0';
        
        delete [] buffer;


        sqlite3_stmt *stmt;

        
        
        if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "SELECT snippet FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
	    {
	    	// Then bind the two ? parameters in the SQLite SQL to the real parameter values
	    	sqlite3_bind_text(stmt, 1, tempScope , -1, SQLITE_STATIC);
	    	sqlite3_bind_text(stmt, 2, tempTriggerText, -1, SQLITE_STATIC);

	    	// Run the query with sqlite3_step
	    	if(SQLITE_ROW == sqlite3_step(stmt))  // SQLITE_ROW 100 sqlite3_step() has another row ready
	    	{
                const char* snippetText = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)); // The 0 here means we only take the first column returned. And it is the snippet as there is only one column
                
                char* previewText = new char[500];
                strcpy(previewText,"[");
                strcat(previewText, tempTriggerText);//TODO: showing the triggertext on the title "snippet preview" instead
                strcat(previewText,"]:\r\n");
                char* contentTruncated = new char[155];
                strncpy(contentTruncated, snippetText, 154);
                contentTruncated[154]='\0';
                //strcat(contentTruncated,"\0");
                strcat(previewText,contentTruncated);
                if (strlen(contentTruncated)>=153) strcat(previewText, ". . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . "); 
                
                TCHAR* previewTextWide = toWideChar(previewText);
                //TODO: investigate why all eol are messed up in preview box

                //size_t origsize = strlen(snippetText) + 1; 
                //size_t convertedChars = 0; 
                //wchar_t previewText[270]; 
                //mbstowcs_s(&convertedChars, previewText, 190, snippetText, _TRUNCATE);
                //
                //if (convertedChars>=184)
                //{
                //    const TCHAR etcText[] = TEXT(" . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .");
                //    ::_tcscat_s(previewText,etcText);
                //}
                
                snippetDock.setDlgText(ID_SNIPSHOW_EDIT,previewTextWide);

                delete [] previewText;
                delete [] contentTruncated;
                delete [] previewTextWide;

                //wchar_t countText[10];
                //::_itow_s(convertedChars, countText, 10, 10); 
                //::MessageBox(nppData._nppHandle, countText, TEXT(PLUGIN_NAME), MB_OK);
	    	}	
	    }
	    sqlite3_finalize(stmt);
    } else
    {
        
        snippetDock.setDlgText(ID_SNIPSHOW_EDIT,TEXT("Select an item in SnippetDock to view the snippet preview here."));
    }
    delete [] bufferWide;
    //::SendMessage(curScintilla,SCI_GRABFOCUS,0,0); 
}

void insertHotSpotSign()
{
    insertTagSign("$[![]!]");
}

//void insertWarmSpotSign()
//{
//    insertTagSign("${!{}!}");
//}
//void insertChainSnippetSign()
//{
//    insertTagSign("$[![(cha)snippetname]!]");
//}
//void insertKeyWordSpotSign()
//{
//    insertTagSign("$[![(key)somekeyword]!]");
//}
//void insertCommandLineSign()
//{
//    insertTagSign("$[![(cmd)somecommand]!]");
//}

void insertTagSign(char * tagSign)
{
    //HWND curScintilla = getCurrentScintilla();
    int posStart = ::SendScintilla(SCI_GETSELECTIONSTART,0,0);
    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)tagSign);
    //int posCurrent = ::SendMessage(curScintilla,SCI_GETCURRENTPOS,0,0);
    ::SendScintilla(SCI_GOTOPOS,posStart+4,0);
    //if (g_editorView)
    //{
        //HWND curScintilla = getCurrentScintilla();
        //int posCurrent = ::SendMessage(curScintilla,SCI_GETCURRENTPOS,0,0);
        //::SendMessage(curScintilla,SCI_GOTOPOS,posCurrent,0);
        //::SendMessage(curScintilla,SCI_REPLACESEL,0,(LPARAM)tagSign);
        //::SendMessage(curScintilla,SCI_SETSEL,posCurrent+strlen(tagSign)-3-11,posCurrent+strlen(tagSign)-3);
        ////::SendMessage(curScintilla,SCI_GOTOPOS,posCurrent+strlen(tagSign)-3,0);
    //} else
    //{
    //    ::MessageBox(nppData._nppHandle, TEXT("Hotspots can be inserted only when you are editing snippets."), TEXT(PLUGIN_NAME), MB_OK);
    //}
}


bool replaceTag(char *expanded, int &posCurrent, int &posBeforeTag)
{
    
    //TODO: can use ::SendMessage(curScintilla, SCI_ENSUREVISIBLE, line-1, 0); to make sure that caret is visible after long snippet substitution.
    //TODO: should abandon this `[SnippetInserting] method
    int lineCurrent = ::SendScintilla(SCI_LINEFROMPOSITION, posCurrent, 0);
    int initialIndent = ::SendScintilla(SCI_GETLINEINDENTATION, lineCurrent, 0);

    ::SendScintilla(SCI_INSERTTEXT, posCurrent, (LPARAM)"____`[SnippetInserting]");

	::SendScintilla(SCI_SETTARGETSTART, posBeforeTag, 0);
	::SendScintilla(SCI_SETTARGETEND, posCurrent, 0);
    ::SendScintilla(SCI_REPLACETARGET, strlen(expanded), reinterpret_cast<LPARAM>(expanded));

    searchNext("`[SnippetInserting]");
    int posEndOfInsertedText = ::SendScintilla(SCI_GETCURRENTPOS,0,0)+19;

    // adjust indentation according to initial indentation
    if (pc.configInt[INDENT_REFERENCE]==1)
    {
        int lineInsertedSnippet = ::SendScintilla(SCI_LINEFROMPOSITION, posEndOfInsertedText, 0);

        int lineIndent=0;
        for (int i=lineCurrent+1;i<=lineInsertedSnippet;i++)
        {
            lineIndent = ::SendScintilla(SCI_GETLINEINDENTATION, i, 0);
            ::SendScintilla(SCI_SETLINEINDENTATION, i, initialIndent+lineIndent);
        }
    }
    searchNext("`[SnippetInserting]");
    posEndOfInsertedText = ::SendScintilla(SCI_GETCURRENTPOS,0,0)+19;
                    
    ::SendScintilla(SCI_GOTOPOS,posBeforeTag,0);
    searchNext("[>END<]");
    int posEndOfSnippet = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
        
    ::SendScintilla(SCI_SETSELECTION,posEndOfSnippet,posEndOfInsertedText);
    ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)"");
    
	//::SendScintilla(SCI_GOTOPOS, posCurrent, 0);
    return true;
}


int getCurrentTag(int posCurrent, char **buffer, int triggerLength)
{
	int length = -1;

    int posBeforeTag;
    if (triggerLength<=0)
    {
        //TODO: global variable for word Char?
        char wordChar[MAX_PATH]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

        //alertNumber(wcslen(pc.configText[CUSTOM_ESCAPE_CHAR]));
        //TODO: potential performance improvement by forming the wordchar with escape char that the initialization
        if (wcslen(pc.configText[CUSTOM_ESCAPE_CHAR])>0)
        {
            char *customEscapeChar = toCharArray(pc.configText[CUSTOM_ESCAPE_CHAR]);
            strcat(wordChar,customEscapeChar);
            delete [] customEscapeChar;
        }
        

        //if (g_escapeChar == 1)
        //{
        //    strcat(wordChar,"<");
        //}

        ::SendScintilla(SCI_SETWORDCHARS, 0, (LPARAM)wordChar);
	    posBeforeTag = static_cast<int>(::SendScintilla(SCI_WORDSTARTPOSITION, posCurrent, 1));
        ::SendScintilla(SCI_SETCHARSDEFAULT, 0, 0);
    } else
    {
        posBeforeTag = posCurrent - triggerLength;
    }
                
    if (posCurrent - posBeforeTag < 100) // Max tag length 100
    {
        

        //*buffer = new char[(posCurrent - posBeforeTag) + 1];
		//Sci_TextRange tagRange;
		//tagRange.chrg.cpMin = posBeforeTag;
		//tagRange.chrg.cpMax = posCurrent;
		//tagRange.lpstrText = *buffer;
        //
	    //::SendScintilla(SCI_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tagRange));
        sciGetText(&*buffer, posBeforeTag, posCurrent);

		length = (posCurrent - posBeforeTag);
	}
    
	return length;
}



void showSnippetDock()
{
	snippetDock.setParent(nppData._nppHandle);
	tTbData	data = {0};

	if (!snippetDock.isCreated())
	{
		snippetDock.create(&data);

		// define the default docking behaviour
		data.uMask = DWS_DF_CONT_RIGHT;
		data.pszModuleName = snippetDock.getPluginFileName();

		// the dlgDlg should be the index of funcItem where the current function pointer is
		data.dlgID = g_snippetDockIndex;
		::SendMessage(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&data);

        //This determine the initial state of the snippetdock.
        snippetDock.display();
	} else
    {
        snippetDock.display(!snippetDock.isVisible());
    }
    updateDockItems();
    //snippetHintUpdate();
    
}


void snippetHintUpdate()
{     
    if ((!g_editorView) && (pc.configInt[LIVE_HINT_UPDATE]==1) && (g_rectSelection==false))
    {
        if (snippetDock.isVisible())
        {
            pc.configInt[LIVE_HINT_UPDATE]=0;
            //HWND curScintilla = getCurrentScintilla();
            //if ((::SendScintilla(SCI_GETMODIFY,0,0)!=0) && (::SendScintilla(SCI_SELECTIONISRECTANGLE,0,0)==0))
            if (::SendScintilla(SCI_SELECTIONISRECTANGLE,0,0) == 0)
            {
                
                int posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                char *partialTag;
	            int tagLength = getCurrentTag(posCurrent, &partialTag);
                
                if (tagLength==0)
                {
                    updateDockItems(false,false);
                } else if ((tagLength>0) && (tagLength<20))
                {
                    //alertNumber(tagLength);
                    char similarTag[MAX_PATH]="";
                    if (pc.configInt[INCLUSIVE_TRIGGERTEXT_COMPLETION]==1) strcat(similarTag,"%");
                    strcat(similarTag,partialTag);
                    strcat(similarTag,"%");
            
                    updateDockItems(false,false,similarTag);
                }
                
                if (tagLength>=0) delete [] partialTag;   
            }
            pc.configInt[LIVE_HINT_UPDATE]=1;
        }
    }
    //if (g_modifyResponse) refreshAnnotation();
}

void updateDockItems(bool withContent, bool withAll, char* tag, bool populate)
{   
    pc.configInt[LIVE_HINT_UPDATE]--;

    int scopeLength=0;
    int triggerLength=0;
    int contentLength=0;
    int tempScopeLength=0;
    int tempTriggerLength=0;
    int tempContentLength=0;

    //g_snippetCacheSize=snippetDock.getLength();
    
    clearCache();

    snippetDock.clearDock();
    sqlite3_stmt *stmt;
    

    if (g_editorView) withAll = true;
    //TODO: there is a bug in the withAll option. The list is limited by the pc.configInt[SNIPPET_LIST_LENGTH], which is not a desirable effect

    // TODO: Use strcat instead of just nested if 
    int sqlitePrepare;
    if (pc.configInt[SNIPPET_LIST_ORDER_TAG_TYPE]==1)
    {
        if (withAll)
        {
            sqlitePrepare = sqlite3_prepare_v2(g_db, "SELECT tag,tagType,snippet FROM snippets ORDER BY tagType DESC,tag DESC LIMIT ? ", -1, &stmt, NULL);
        } else 
        {
            sqlitePrepare = sqlite3_prepare_v2(g_db, "SELECT tag,tagType,snippet FROM snippets WHERE (tagType LIKE ? OR tagType LIKE ? OR tagType LIKE ? OR tagType LIKE ? OR tagType LIKE ?) AND tag LIKE ? ORDER BY tagType DESC,tag DESC LIMIT ? ", -1, &stmt, NULL);
        }
    } else
    {
        if (withAll)
        {
            sqlitePrepare = sqlite3_prepare_v2(g_db, "SELECT tag,tagType,snippet FROM snippets ORDER BY tag DESC,tagType DESC LIMIT ? ", -1, &stmt, NULL);
        } else 
        {
            sqlitePrepare = sqlite3_prepare_v2(g_db, "SELECT tag,tagType,snippet FROM snippets WHERE (tagType LIKE ? OR tagType LIKE ? OR tagType LIKE ? OR tagType LIKE ? OR tagType LIKE ?) AND tag LIKE ? ORDER BY tag DESC,tagType DESC LIMIT ? ", -1, &stmt, NULL);
        }
    }
    
    
	if (g_dbOpen && SQLITE_OK == sqlitePrepare)
	{
        char *customScope = new char[MAX_PATH];
        
        char *tagType1 = NULL;
        TCHAR *fileType1 = new TCHAR[MAX_PATH];
        char *tagType2 = NULL;
        TCHAR *fileType2 = new TCHAR[MAX_PATH];

        if (withAll)
        {
            char snippetCacheSizeText[10];
            ::_itoa(pc.configInt[SNIPPET_LIST_LENGTH], snippetCacheSizeText, 10); 
            sqlite3_bind_text(stmt, 1, snippetCacheSizeText, -1, SQLITE_STATIC);
        } else
        {   
            customScope = toCharArray(pc.configText[CUSTOM_SCOPE]);
            sqlite3_bind_text(stmt, 1, customScope, -1, SQLITE_STATIC);
            
            ::SendMessage(nppData._nppHandle, NPPM_GETEXTPART, (WPARAM)MAX_PATH, (LPARAM)fileType1);
            tagType1 = toCharArray(fileType1);
            sqlite3_bind_text(stmt, 2, tagType1, -1, SQLITE_STATIC);

            ::SendMessage(nppData._nppHandle, NPPM_GETNAMEPART, (WPARAM)MAX_PATH, (LPARAM)fileType2);
            tagType2 = toCharArray(fileType2);
            sqlite3_bind_text(stmt, 3, tagType2, -1, SQLITE_STATIC);

            sqlite3_bind_text(stmt, 4, getLangTagType(), -1, SQLITE_STATIC);
        
            sqlite3_bind_text(stmt, 5, "GLOBAL", -1, SQLITE_STATIC);

            sqlite3_bind_text(stmt, 6, tag, -1, SQLITE_STATIC);

            //TODO: potential performance improvement by just setting 100
            char snippetCacheSizeText[10];
            ::_itoa(pc.configInt[SNIPPET_LIST_LENGTH], snippetCacheSizeText, 10); 
            sqlite3_bind_text(stmt, 7, snippetCacheSizeText, -1, SQLITE_STATIC);
        }
        
        int row = 0;

        while(true)
        {
            if(SQLITE_ROW == sqlite3_step(stmt))
            {
                const char* tempTrigger = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                if ((withAll) || (tempTrigger[0]!='_'))
                {
                    tempTriggerLength = strlen(tempTrigger)*4 + 1;
                    if (tempTriggerLength> triggerLength)
                    {
                        triggerLength = tempTriggerLength;
                    }
                    g_snippetCache[row].triggerText = new char[strlen(tempTrigger)*4 + 1];
                    strcpy(g_snippetCache[row].triggerText, tempTrigger);

                    const char* tempScope = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
                    tempScopeLength = strlen(tempScope)*4 + 1;
                    if (tempScopeLength> scopeLength)
                    {
                        scopeLength = tempScopeLength;
                    }
                    g_snippetCache[row].scope = new char[strlen(tempScope)*4 + 1];
                    strcpy(g_snippetCache[row].scope, tempScope);



                    if (withContent)
                    {
                        const char* tempContent = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                        tempContentLength = strlen(tempContent)*4 + 1;
                        if (tempContentLength> contentLength)
                        {
                            contentLength = tempContentLength;
                        }
                        g_snippetCache[row].content = new char[strlen(tempContent)*4 + 1];
                        strcpy(g_snippetCache[row].content, tempContent);
                    }
                    row++;


                }
            }
            else
            {
                break;  
            }
        }

        delete [] customScope;
        delete [] tagType1;
        delete [] fileType1;
        delete [] tagType2;
        delete [] fileType2;
    }
    sqlite3_finalize(stmt);
    if (populate) populateDockItems();
    
    if (pc.configInt[LIVE_PREVIEW_BOX]==1) showPreview(true);

    pc.configInt[LIVE_HINT_UPDATE]++;
    //::SendMessage(getCurrentScintilla(),SCI_GRABFOCUS,0,0);   
    
}

void deleteCache()
{   

    
    for (int i=0;i<pc.configInt[SNIPPET_LIST_LENGTH];i++)
    {
        delete [] g_snippetCache[i].triggerText;
        delete [] g_snippetCache[i].scope;
        delete [] g_snippetCache[i].content;
        
    }
}

void populateDockItems()
{
    //TODO: Use 2 columns of list box, or list control
    for (int j=0;j<pc.configInt[SNIPPET_LIST_LENGTH];j++)
    {
        if (g_snippetCache[j].scope !=NULL)
        {
            char newText[300]="";
            
            int triggerTextLength = strlen(g_snippetCache[j].triggerText);
            
            //TODO: option to show trigger text first

            //if (strcmp(g_snippetCache[j].scope,"GLOBAL")==0)
            //{
            //    strcat(newText,"<<");
            //    strcat(newText,g_snippetCache[j].scope);
            //    strcat(newText,">>");
            //} else
            //{

            strcat(newText,"<");
            strcat(newText,g_snippetCache[j].scope);
            strcat(newText,">");
            //}
            
            //TODO: make this 14 customizable in settings
            int scopeLength = 14 - strlen(newText);
            if (scopeLength < 3) scopeLength = 3;
            for (int i=0;i<scopeLength;i++)
            {
                strcat(newText," ");
            }
            strcat(newText,g_snippetCache[j].triggerText);

            //size_t origsize = strlen(newText) + 1;
            //const size_t newsize = 400;
            //size_t convertedChars = 0;
            //wchar_t convertedTagText[newsize];
            //mbstowcs_s(&convertedChars, convertedTagText, origsize, newText, _TRUNCATE);

            wchar_t* convertedTagText = toWideChar(newText);

            snippetDock.addDockItem(convertedTagText);
            delete [] convertedTagText;


        }
    }
    deleteCache();
}

void clearCache()
{   

    
    //TODO: fix update dockitems memoryleak
    //g_snippetCacheSize=pc.configInt[SNIPPET_LIST_LENGTH];
    
    for (int i=0;i<pc.configInt[SNIPPET_LIST_LENGTH];i++)
    {
        //delete [] g_snippetCache[i].triggerText;
        //delete [] g_snippetCache[i].scope;
        //delete [] g_snippetCache[i].content;
        g_snippetCache[i].triggerText=NULL;
        g_snippetCache[i].scope=NULL;
        g_snippetCache[i].content=NULL;
    }
    
    

}

void exportAndClearSnippets()
{
    //TODO: move the snippet export counting message out of the export snippets function so that it can be shown together with the clear snippet message
    if (exportSnippets())
    {
        int messageReturn = showMessageBox(TEXT("Are you sure that you want to clear the whole snippet database?"),MB_YESNO);
        //int messageReturn = ::MessageBox(nppData._nppHandle, TEXT("Are you sure that you want to clear the whole snippet database?"), TEXT(PLUGIN_NAME), MB_YESNO);
        if (messageReturn == IDYES)
        {
            clearAllSnippets();
            showMessageBox(TEXT("All snippets are deleted."));
            //::MessageBox(nppData._nppHandle, TEXT("All snippets are deleted."), TEXT(PLUGIN_NAME), MB_OK);
        } else 
        {
            showMessageBox(TEXT("Snippet clearing is aborted."));
            //::MessageBox(nppData._nppHandle, TEXT("Snippet clearing is aborted."), TEXT(PLUGIN_NAME), MB_OK);
        }
    }
}

void exportSnippetsOnly()
{
    exportSnippets();
}

void clearAllSnippets()
{
    sqlite3_stmt *stmt;
                
    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "DELETE FROM snippets", -1, &stmt, NULL))
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "VACUUM", -1, &stmt, NULL))
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    updateDockItems(false,false);
}

bool exportSnippets()
{
    //TODO: Can actually add some informtiaon at the end of the exported snippets......can be useful information like version number or just describing the package

    pc.configInt[LIVE_HINT_UPDATE]--;  // Temporary turn off live update as it disturb exporting

    bool success = false;

    OPENFILENAME ofn;
    char fileName[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = TEXT("FingerText Datafiles (*.ftd)\0*.ftd\0");
    ofn.lpstrFile = (LPWSTR)fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = TEXT("");
    
    if (::GetSaveFileName(&ofn))
    {
        ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
        int importEditorBufferID = ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
        ::SendMessage(nppData._nppHandle, NPPM_SETBUFFERENCODING, (WPARAM)importEditorBufferID, 4);

        ::SendScintilla(SCI_SETCURSOR, SC_CURSORWAIT, 0);
        pc.configInt[SNIPPET_LIST_LENGTH] = 100000;
        g_snippetCache = new SnipIndex [pc.configInt[SNIPPET_LIST_LENGTH]];
        updateDockItems(true,true,"%", false);
        
        int exportCount = 0;
        for (int j=0;j<pc.configInt[SNIPPET_LIST_LENGTH];j++)
        {
            if (g_snippetCache[j].scope !=NULL)
            {
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)g_snippetCache[j].triggerText);
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"\r\n");
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)g_snippetCache[j].scope);
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"\r\n");
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)g_snippetCache[j].content);
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"!$[FingerTextData FingerTextData]@#\r\n");
                exportCount++;
            }
        }
        ::SendMessage(nppData._nppHandle, NPPM_SAVECURRENTFILEAS, 0, (LPARAM)fileName);
        success = true;

        ::SendScintilla(SCI_SETCURSOR, SC_CURSORNORMAL, 0);
        wchar_t exportCountText[35] = TEXT("");

        if (exportCount>1)
        {
            ::_itow_s(exportCount, exportCountText, 10, 10);
            wcscat_s(exportCountText,TEXT(" snippets are exported."));
        } else if (exportCount==1)
        {
            wcscat_s(exportCountText,TEXT("1 snippet is exported."));
        } else
        {
            wcscat_s(exportCountText,TEXT("No snippets are exported."));
        }
        
        ::SendScintilla(SCI_SETSAVEPOINT,0,0);
        ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_CLOSE);
        showMessageBox(exportCountText);
        //::MessageBox(nppData._nppHandle, exportCountText, TEXT(PLUGIN_NAME), MB_OK);
    }
    pc.configInt[SNIPPET_LIST_LENGTH] = GetPrivateProfileInt(TEXT(PLUGIN_NAME), TEXT("snippet_list_length"), 1000 , pc.iniPath); // TODO: This hard coding of DEFAULT_SNIPPET_LIST_LENGTH is temporary and can cause problem.
    g_snippetCache = new SnipIndex [pc.configInt[SNIPPET_LIST_LENGTH]];
    updateDockItems(true,true,"%");
    pc.configInt[LIVE_HINT_UPDATE]++;

    return success;
}

//TODO: importsnippet and savesnippets need refactoring sooooo badly
//TODO: Or it should be rewrite, import snippet should open the snippetediting.ftb, turn or annotation, and cut and paste the snippet on to that file and use the saveSnippet function
void importSnippets()
{
    
    //TODO: importing snippet will change the current directory, which is not desirable effect
    if (::SendMessage(nppData._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)g_ftbPath))
    {
        //TODO: prompt for closing tab instead of just warning
        showMessageBox(TEXT("Please close all the snippet editing tabs (SnippetEditor.ftb) before importing any snippet pack."));
        //::MessageBox(nppData._nppHandle, TEXT("Please close all the snippet editing tabs (SnippetEditor.ftb) before importing any snippet pack."), TEXT(PLUGIN_NAME), MB_OK);
        return;
    }

    if (::SendMessage(nppData._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)g_fttempPath))
    {
        ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_SAVE);
        ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_CLOSE);
    }   

    pc.configInt[LIVE_HINT_UPDATE]--;
    
    OPENFILENAME ofn;
    char fileName[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = TEXT("FingerText Datafiles (*.ftd)\0*.ftd\0");
    ofn.lpstrFile = (LPWSTR)fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = TEXT("");
    
    if (::GetOpenFileName(&ofn))
    {

        //int conflictOverwrite = IDNO;
        //if (g_importOverWriteOption==1)
        //{
        //   conflictOverwrite = ::MessageBox(nppData._nppHandle, TEXT("Do you want to overwrite the database when the imported snippets has conflicts with existing snippets? Press Yes if you want to overwrite, No if you want to keep both versions."), TEXT(PLUGIN_NAME), MB_YESNO);
        //}
        int conflictKeepCopy = IDNO;
        conflictKeepCopy = showMessageBox(TEXT("Do you want to keep both versions if the imported snippets are conflicting with existing one?\r\n\r\nYes - Keep both versions\r\nNo - Overwrite existing version\r\nCancel - Stop importing"),MB_YESNOCANCEL);
        //conflictKeepCopy = ::MessageBox(nppData._nppHandle, TEXT("Do you want to keep both versions if the imported snippets are conflicting with existing one?\r\n\r\nYes - Keep both versions\r\nNo - Overwrite existing version\r\nCancel - Stop importing"), TEXT(PLUGIN_NAME), MB_YESNOCANCEL);

        if (conflictKeepCopy == IDCANCEL)
        {
            showMessageBox(TEXT("Snippet importing aborted."));
            //::MessageBox(nppData._nppHandle, TEXT("Snippet importing aborted."), TEXT(PLUGIN_NAME), MB_OK);
            return;
        }


        //::MessageBox(nppData._nppHandle, (LPCWSTR)fileName, TEXT(PLUGIN_NAME), MB_OK);
        std::ifstream file;
        //file.open((LPCWSTR)fileName, std::ios::binary | std::ios::in);     //TODO: verified why this doesn't work. Specifying the binary thing will cause redundant copy keeping when importing
        file.open((LPCWSTR)fileName); // TODO: This part may cause problem in chinese file names

        file.seekg(0, std::ios::end);
        int fileLength = file.tellg();
        file.seekg(0, std::ios::beg);

        if (file.is_open())
        {
            char* fileText = new char[fileLength+1];
            ZeroMemory(fileText,fileLength);

            file.read(fileText,fileLength);
            fileText[fileLength] = '\0';
            file.close();
        
            ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
            int importEditorBufferID = ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
            ::SendMessage(nppData._nppHandle, NPPM_SETBUFFERENCODING, (WPARAM)importEditorBufferID, 4);
        
            //HWND curScintilla = getCurrentScintilla();
            ::SendScintilla(SCI_SETCURSOR, SC_CURSORWAIT, 0);

            //::SendMessage(curScintilla, SCI_SETCODEPAGE,65001,0);
            ::SendScintilla(SCI_SETTEXT, 0, (LPARAM)fileText);
            ::SendScintilla(SCI_GOTOPOS, 0, 0);
            ::SendScintilla(SCI_NEWLINE, 0, 0);

            delete [] fileText;
        
            int importCount=0;
            int conflictCount=0;
            int next=0;
            char* snippetText;
            char* tagText; 
            char* tagTypeText;
            int snippetPosStart;
            int snippetPosEnd;
            bool notOverWrite;
            char* snippetTextOld;
            //char* snippetTextOldCleaned;
            
            do
            {
                //import snippet do not have the problem of " " in save snippet because of the space in  "!$[FingerTextData FingerTextData]@#"
                ::SendScintilla(SCI_GOTOPOS, 0, 0);
                                
                getLineChecked(&tagText,1,TEXT("Error: Invalid TriggerText. The ftd file may be corrupted."));
                getLineChecked(&tagTypeText,2,TEXT("Error: Invalid Scope. The ftd file may be corrupted."));
                
                // Getting text after the 3rd line until the tag !$[FingerTextData FingerTextData]@#
                ::SendScintilla(SCI_GOTOLINE,3,0);
                snippetPosStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                //int snippetPosEnd = ::SendMessage(curScintilla,SCI_GETLENGTH,0,0);
            
                searchNext("!$[FingerTextData FingerTextData]@#");
                snippetPosEnd = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                
                //::SendScintilla(SCI_SETSELECTION,snippetPosStart,snippetPosEnd);
                //snippetText = new char[snippetPosEnd-snippetPosStart + 1];
                //::SendScintilla(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(snippetText));
                sciGetText(&snippetText,snippetPosStart,snippetPosEnd);


                ::SendScintilla(SCI_SETSELECTION,0,snippetPosEnd+1); // This +1 corrupt the ! in !$[FingerTextData FingerTextData]@# so that the program know a snippet is finished importing
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");

                sqlite3_stmt *stmt;
                
                notOverWrite = false;
                
                if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "SELECT snippet FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
                {
                    sqlite3_bind_text(stmt, 1, tagTypeText, -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 2, tagText, -1, SQLITE_STATIC);
                    if(SQLITE_ROW == sqlite3_step(stmt))
                    {
                        const char* extracted = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
                        
                        snippetTextOld = new char[strlen(extracted)+1];
                        ZeroMemory(snippetTextOld,sizeof(snippetTextOld));
                        strcpy(snippetTextOld, extracted);
                        //
                        //snippetTextOldCleaned = new char[strlen(snippetTextOld)];
                        //ZeroMemory(snippetTextOldCleaned,sizeof(snippetTextOldCleaned));
                        
                        //snippetTextOldCleaned = quickStrip(snippetTextOld,'\r');
                        snippetTextOld = quickStrip(snippetTextOld,'\r');

                        //if (strlen(snippetTextNew) == strlen(snippetText)) alert();
                        //if (strcmp(snippetText,snippetTextOldCleaned) == 0)
                        if (strcmp(snippetText,snippetTextOld) == 0)
                        {
                            //delete [] snippetTextOld;
                            notOverWrite = true;
                            //sqlite3_finalize(stmt);
                        } else
                        {
                            //delete [] snippetTextOld;
                        //    sqlite3_finalize(stmt);
                            if (conflictKeepCopy==IDNO)
                            {
                                if (pc.configInt[IMPORT_OVERWRITE_CONFIRM] == 1)
                                {
                                    // TODO: may be moving the message to earlier location so that the text editor will be showing the message that is about to be overwriting into the database
                                    // TODO: try showing the conflict message on the editor
                        
                                    ::SendScintilla(SCI_GOTOLINE,0,0);
                                    //TODO: refactor this repeated replacesel action
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"\r\nConflicting Snippet: \r\n\r\n     ");
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)tagText);
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"  <");
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)tagTypeText);
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)">\r\n");
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"\r\n\r\n   (More details of the conflicts will be shown in future releases)");
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n----------------------------------------\r\n");
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"---------- [ Pending Imports ] ---------\r\n");
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"----------------------------------------\r\n");
                        
                                    int messageReturn = showMessageBox(TEXT("A snippet already exists, overwrite?"),MB_YESNO);
                                    //int messageReturn = ::MessageBox(nppData._nppHandle, TEXT("A snippet already exists, overwrite?"), TEXT(PLUGIN_NAME), MB_YESNO);
                                    if (messageReturn==IDNO)
                                    {
                                        //delete [] tagText;
                                        //delete [] tagTypeText;
                                        //delete [] snippetText;
                                        // not overwrite
                                        //::MessageBox(nppData._nppHandle, TEXT("The Snippet is not saved."), TEXT(PLUGIN_NAME), MB_OK);
                                        notOverWrite = true;
                                    } else
                                    {
                                        // delete existing entry
                                        if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "DELETE FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
                                        {
                                            sqlite3_bind_text(stmt, 1, tagTypeText, -1, SQLITE_STATIC);
                                            sqlite3_bind_text(stmt, 2, tagText, -1, SQLITE_STATIC);
                                            sqlite3_step(stmt);
                                        } else
                                        {
                                            showMessageBox(TEXT("Cannot write into database."));
                                            //::MessageBox(nppData._nppHandle, TEXT("Cannot write into database."), TEXT(PLUGIN_NAME), MB_OK);
                                        }
                        
                                    }
                                    ::SendScintilla(SCI_GOTOLINE,17,0);
                                    ::SendScintilla(SCI_SETSELECTION,0,::SendScintilla(SCI_GETCURRENTPOS,0,0));
                                    ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
                        
                                } else
                                {
                                    // delete existing entry
                                    if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "DELETE FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
                                    {
                                        sqlite3_bind_text(stmt, 1, tagTypeText, -1, SQLITE_STATIC);
                                        sqlite3_bind_text(stmt, 2, tagText, -1, SQLITE_STATIC);
                                        sqlite3_step(stmt);
                                    } else
                                    {
                                        showMessageBox(TEXT("Cannot write into database."));
                                        //::MessageBox(nppData._nppHandle, TEXT("Cannot write into database."), TEXT(PLUGIN_NAME), MB_OK);
                                    }
                                }
                            } else
                            {
                                notOverWrite = true;
                                //Delete the old entry
                                if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "DELETE FROM snippets WHERE tagType LIKE ? AND tag LIKE ?", -1, &stmt, NULL))
                                {
                                    sqlite3_bind_text(stmt, 1, tagTypeText, -1, SQLITE_STATIC);
                                    sqlite3_bind_text(stmt, 2, tagText, -1, SQLITE_STATIC);
                                    sqlite3_step(stmt);
                                    sqlite3_finalize(stmt);

                                }
                                //write the new entry
                                if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "INSERT INTO snippets VALUES(?,?,?)", -1, &stmt, NULL))
                                {
                                    sqlite3_bind_text(stmt, 1, tagText, -1, SQLITE_STATIC);
                                    sqlite3_bind_text(stmt, 2, tagTypeText, -1, SQLITE_STATIC);
                                    sqlite3_bind_text(stmt, 3, snippetText, -1, SQLITE_STATIC);
                                
                                    sqlite3_step(stmt);
                                    sqlite3_finalize(stmt);
                                }

                                //write the old entry back with conflict suffix
                                if (g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "INSERT INTO snippets VALUES(?,?,?)", -1, &stmt, NULL))
                                {
                                    importCount++;
                                    //TODO: add file name to the stamp
                                    char* dateText = getDateTime("yyyyMMdd");
                                    char* timeText = getDateTime("HHmmss",false);
                                    char* tagTextsuffixed;
                                    tagTextsuffixed = new char [strlen(tagText)+256];
                                
                                    strcpy(tagTextsuffixed,tagText);
                                    strcat(tagTextsuffixed,".OldCopy");
                                    strcat(tagTextsuffixed,dateText);
                                    strcat(tagTextsuffixed,timeText);
                                    
                                    sqlite3_bind_text(stmt, 1, tagTextsuffixed, -1, SQLITE_STATIC);
                                    sqlite3_bind_text(stmt, 2, tagTypeText, -1, SQLITE_STATIC);
                                    sqlite3_bind_text(stmt, 3, snippetTextOld, -1, SQLITE_STATIC);
                                
                                    sqlite3_step(stmt);


                                    //sqlite3_finalize(stmt);
                                    conflictCount++;
                                    delete [] tagTextsuffixed;
                                    delete [] dateText;
                                    delete [] timeText;
                                }
                            }
                        }
                        delete [] snippetTextOld;
                    } else
                    {
                        sqlite3_finalize(stmt);
                    }
                }
            
                if (notOverWrite == false && g_dbOpen && SQLITE_OK == sqlite3_prepare_v2(g_db, "INSERT INTO snippets VALUES(?,?,?)", -1, &stmt, NULL))
                {
                    
                    importCount++;
                    // Then bind the two ? parameters in the SQLite SQL to the real parameter values
                    sqlite3_bind_text(stmt, 1, tagText, -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 2, tagTypeText, -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 3, snippetText, -1, SQLITE_STATIC);
            
                    // Run the query with sqlite3_step
                    sqlite3_step(stmt); // SQLITE_ROW 100 sqlite3_step() has another row ready
                    //::MessageBox(nppData._nppHandle, TEXT("The Snippet is saved."), TEXT(PLUGIN_NAME), MB_OK);
                }
                sqlite3_finalize(stmt);
                //delete [] tagText;
                //delete [] tagTypeText;
                //delete [] snippetText;
            
                ::SendScintilla(SCI_SETSAVEPOINT,0,0);
                updateDockItems(false,false);
            
                ::SendScintilla(SCI_GOTOPOS,0,0);
                next = searchNext("!$[FingerTextData FingerTextData]@#");
            } while (next>=0);
            
            ::SendScintilla(SCI_SETCURSOR, SC_CURSORNORMAL, 0);

            wchar_t importCountText[200] = TEXT("");
            
            if (importCount>1)
            {
                ::_itow_s(importCount, importCountText, 10, 10);
                wcscat_s(importCountText,TEXT(" snippets are imported."));
            } else if (importCount==1)
            {
                wcscat_s(importCountText,TEXT("1 snippet is imported."));
            } else
            {
                wcscat_s(importCountText,TEXT("No Snippets are imported."));
            }

            if (conflictCount>0)
            {
                //TODO: more detail messages and count the number of conflict or problematic snippets
                wcscat_s(importCountText,TEXT("\r\n\r\nThere are some conflicts between the imported and existing snippets. You may go to the snippet editor to clean them up."));
            }
            //::MessageBox(nppData._nppHandle, TEXT("Complete importing snippets"), TEXT(PLUGIN_NAME), MB_OK);
            showMessageBox(importCountText);
            //::MessageBox(nppData._nppHandle, importCountText, TEXT(PLUGIN_NAME), MB_OK);
            
            ::SendScintilla(SCI_SETSAVEPOINT,0,0);
            ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_CLOSE);

                //updateMode();
                //updateDockItems();
        }
        //delete [] fileText;
    }
    pc.configInt[LIVE_HINT_UPDATE]++;

}

int promptSaveSnippet(TCHAR* message)
{
    int messageReturn = IDNO;
    if (g_editorView)
    {
        //HWND curScintilla = ::getCurrentScintilla();

        if (message == NULL)
        {
            saveSnippet();
        } else if (::SendScintilla(SCI_GETMODIFY,0,0)!=0)
        {
            messageReturn = showMessageBox(message,MB_YESNO);
            //messageReturn=::MessageBox(nppData._nppHandle, message, TEXT(PLUGIN_NAME), MB_YESNO);
            if (messageReturn==IDYES)
            {
                saveSnippet();
            }
        }
    }
    return messageReturn;
}

void updateLineCount(int count)
{
    if (count>=0)
    {
        g_editorLineCount = count;
    } else
    {
        g_editorLineCount = ::SendScintilla(SCI_GETLINECOUNT,0,0);
    }
}


void updateMode()
{
    updateScintilla();
    g_lastTriggerPosition = 0;
    //TODO: should change to edit mode and normal mode by a button, and dynamically adjust the dock content
    //HWND curScintilla = getCurrentScintilla();
    TCHAR fileType[MAX_PATH];
    ::SendMessage(nppData._nppHandle, NPPM_GETFILENAME, (WPARAM)MAX_PATH, (LPARAM)fileType);
        
    if (::_tcscmp(fileType,TEXT("SnippetEditor.ftb"))==0)
    {
        snippetDock.toggleSave(true);
        g_editorView = true;
        snippetDock.setDlgText(IDC_LIST_TITLE, TEXT("EDIT MODE\r\n(Double click item in list to edit another snippet, Ctrl+S to save)"));
        updateLineCount();
    } else if (g_enable)
    {
        snippetDock.toggleSave(false);
        g_editorView = false;
        snippetDock.setDlgText(IDC_LIST_TITLE, TEXT("NORMAL MODE [FingerText Enabled]\r\n(Type trigger text and hit tab to insert snippet)"));
    } else
    {
        snippetDock.toggleSave(false);
        g_editorView = false;
        snippetDock.setDlgText(IDC_LIST_TITLE, TEXT("NORMAL MODE [FingerText Disabled]\r\n(To enable: Plugins>FingerText>Toggle FingerText On/Off)"));
    }
}

void showSettings()
{
    pc.settings();
}

void showHelp()
{
    pc.help();
}

void showAbout()
{
    pc.about();
}

void refreshAnnotation()
{
    if (g_editorView)
    {
        g_selectionMonitor--;
        g_modifyResponse = false;
        TCHAR fileType[MAX_PATH];
        //::SendMessage(nppData._nppHandle, NPPM_GETEXTPART, (WPARAM)MAX_PATH, (LPARAM)fileType);
        ::SendMessage(nppData._nppHandle, NPPM_GETFILENAME, (WPARAM)MAX_PATH, (LPARAM)fileType);
        
        if (::_tcscmp(fileType,TEXT("SnippetEditor.ftb"))==0)
        {
            //HWND curScintilla = getCurrentScintilla();

            int lineCurrent = ::SendScintilla(SCI_LINEFROMPOSITION,::SendScintilla(SCI_GETCURRENTPOS,0,0),0);

            //::SendMessage(getCurrentScintilla(), SCI_ANNOTATIONCLEARALL, 0, 0);
            ::SendScintilla(SCI_ANNOTATIONCLEARALL, 0, 0);

            if (lineCurrent == 1)
            {
                ::SendScintilla(SCI_ANNOTATIONSETTEXT, 0, (LPARAM)"\
__________________________________________________________________________\r\n\
 Snippet Editor Hint: \r\n\r\n\
 Triggertext is the text you type to trigger the snippets.\r\n\
 e.g. \"npp\"(without quotes) means the snippet is triggered \r\n\
 when you type npp and hit tab)\r\n\
__________________________________________________________________________\r\n\r\n\r\n\r\n\
       =============   TriggerText   =============                        ");
            } else if (lineCurrent == 2)
            {
                ::SendScintilla(SCI_ANNOTATIONSETTEXT, 0, (LPARAM)"\
__________________________________________________________________________\r\n\
 Snippet Editor Hint: \r\n\r\n\
 Scope determines where the snippet is available.\r\n\
 e.g. \"GLOBAL\"(without quotes) for globally available snippets.\r\n\
 \".cpp\"(without quotes) means available in .cpp documents and\r\n\
 \"Lang:HTML\"(without quotes) for all html documents.\r\n\
__________________________________________________________________________\r\n\r\n\r\n\
       =============   TriggerText   =============                        ");
            } else
            {
                ::SendScintilla(SCI_ANNOTATIONSETTEXT, 0, (LPARAM)"\
__________________________________________________________________________\r\n\
 Snippet Editor Hint: \r\n\r\n\
 Snippet Content is the text that is inserted to the editor when \r\n\
 a snippet is triggered.\r\n\
 It can be as long as many paragraphs or just several words.\r\n\
 Remember to place an [>END<] at the end of the snippet.\r\n\
__________________________________________________________________________\r\n\r\n\r\n\
       =============   TriggerText   =============                        ");
            }

            ::SendScintilla(SCI_ANNOTATIONSETTEXT, 1, (LPARAM)"\r\n       =============      Scope      =============                        ");
            ::SendScintilla(SCI_ANNOTATIONSETTEXT, 2, (LPARAM)"\r\n       ============= Snippet Content =============                        ");
            ::SendScintilla(SCI_ANNOTATIONSETSTYLE, 0, STYLE_INDENTGUIDE);
            ::SendScintilla(SCI_ANNOTATIONSETSTYLE, 1, STYLE_INDENTGUIDE);
            ::SendScintilla(SCI_ANNOTATIONSETSTYLE, 2, STYLE_INDENTGUIDE);
              
            ::SendScintilla(SCI_ANNOTATIONSETVISIBLE, 2, 0);
            
        }
        g_selectionMonitor++;
        g_modifyResponse = true;
    }
}

//For option dynamic hotspot
void cleanOptionItem()
{
    g_optionArray.clear();
    
    //int i = 0;
    //while (i<g_optionNumber)
    //{
    //    //alertNumber(i);
    //    delete [] g_optionArray[i];
    //    i++;
    //};
    turnOffOptionMode();
    //g_optionMode = false;
    //g_optionNumber = 0;
    g_optionCurrent = 0;
}

//char* getOptionItem()
void updateOptionCurrent(bool toNext)
{
    //char* item;
    //int length = strlen(g_optionArray[g_optionCurrent]);
    //item = new char [length+1];
    //strcpy(item, g_optionArray[g_optionCurrent]);
    if (toNext)
    {
        if (g_optionCurrent >= g_optionArray.size()-1) 
        {
            g_optionCurrent = 0;
        } else
        {
            g_optionCurrent++;
        }
    } else
    {
        if (g_optionCurrent <= 0) 
        {
            g_optionCurrent = g_optionArray.size()-1;
        } else
        {
            g_optionCurrent--;
        }
    }
    //return item;
    //return g_optionArray[g_optionCurrent];
}

void turnOffOptionMode()
{
    //if (g_optionMode) alert();
    g_optionMode = false;
}

void turnOnOptionMode()
{
    g_optionMode = true;
}
void optionNavigate(bool toNext)
{
    
    ::SendScintilla(SCI_SETSELECTION,g_optionStartPosition,g_optionEndPosition);
    updateOptionCurrent(toNext);
    char* option = toCharArray(g_optionArray[g_optionCurrent]);
    ::SendScintilla(SCI_REPLACESEL, 0, (LPARAM)option);
    delete [] option;
    ::SendScintilla(SCI_GOTOPOS,g_optionStartPosition,0);
    g_optionEndPosition = g_optionStartPosition + g_optionArray[g_optionCurrent].length();
    ::SendScintilla(SCI_SETSELECTION,g_optionStartPosition,g_optionEndPosition);
}

void selectionMonitor(int contentChange)
{
    //TODO: backspace at the beginning of line 4 in editor mode breaks hint annotation 
    //TODO: pasting text with more then one line in the scope field will break editor restriction
    //TODO: lots of optimization needed
    //In normal view, this code is going to cater the option navigation. In editor view, it restrict selection in first 3 lines
    if (g_selectionMonitor == 1)
    {
        //TODO: this "100" is associated with the limit of number of multiple hotspots that can be simultaneously activated, should find a way to make this more customizable
        if ((::SendScintilla(SCI_GETSELECTIONMODE,0,0)!=SC_SEL_STREAM) || (::SendScintilla(SCI_GETSELECTIONS,0,0)>100))
        {
            g_rectSelection = true;
        } else
        {
            g_rectSelection = false;
        }

        g_modifyResponse = false;
        g_selectionMonitor--;
        if (g_editorView == false)
        {
            
            //TODO: reexamine possible performance improvement
            if (g_optionMode == true)
            {
               
                int posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                //alertNumber(g_optionStartPosition);
                //alertNumber(posCurrent);
                //TODO: a bug when there is an empty option and the hotspot is at the beginning of document
                if (posCurrent > g_optionStartPosition)
                {
                    optionNavigate(true);
                    //optionTriggered = true;
                    turnOnOptionMode();
                    //g_optionMode = true; // TODO: investigate why this line is necessary
                } else
                {
                    optionNavigate(false);
                    turnOnOptionMode();
                    //g_optionMode = true;
                }
                //else
                //{
                //    cleanOptionItem();
                //    g_optionMode = false;
                //}
            }
        } else if (pc.configInt[EDITOR_CARET_BOUND] == 1)
        {
            int posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
            int lineCurrent = ::SendScintilla(SCI_LINEFROMPOSITION,posCurrent,0);
            int firstlineEnd = ::SendScintilla(SCI_GETLINEENDPOSITION,0,0);
            int currentLineCount = ::SendScintilla(SCI_GETLINECOUNT,0,0);
            int selectionStart = ::SendScintilla(SCI_GETSELECTIONSTART,0,0);
            int selectionEnd = ::SendScintilla(SCI_GETSELECTIONEND,0,0);
            int selectionStartLine = ::SendScintilla(SCI_LINEFROMPOSITION,selectionStart,0);
            int selectionEndLine = ::SendScintilla(SCI_LINEFROMPOSITION,selectionEnd,0);
            
            //alertNumber(lineCurrent);
            //if (contentChange)
            if (contentChange & (SC_UPDATE_CONTENT))
            {
                if ((g_editorLineCount < currentLineCount) && (lineCurrent <= 3))
                {
                    //::SendMessage(curScintilla,SCI_UNDO,0,0);
                    ::SendScintilla(SCI_UNDO,0,0);
                    updateLineCount();
                    //updateLineCount(currentLineCount);
                } else if ((g_editorLineCount > currentLineCount) && (lineCurrent <= 2))
                {
                    ::SendScintilla(SCI_UNDO,0,0);
                    updateLineCount();
                    
                } else if (::SendScintilla(SCI_LINELENGTH,1,0)>=41)
                {
                    showMessageBox(TEXT("The TriggerText length limit is 40 characters."));
                    //::MessageBox(nppData._nppHandle, TEXT("The TriggerText length limit is 40 characters."), TEXT(PLUGIN_NAME), MB_OK);
                    ::SendScintilla(SCI_UNDO,0,0);
                    updateLineCount();
                
                } else if (::SendScintilla(SCI_LINELENGTH,2,0)>=251)
                {
                    showMessageBox(TEXT("The Scope length limit is 250 characters."));
                    //::MessageBox(nppData._nppHandle, TEXT("The Scope length limit is 250 characters."), TEXT(PLUGIN_NAME), MB_OK);
                    ::SendScintilla(SCI_UNDO,0,0);
                    updateLineCount();
                } 
            }

            if (lineCurrent <= 0) ::SendScintilla(SCI_GOTOLINE,1,0);

            if ((selectionStartLine != selectionEndLine) && ((selectionStartLine <= 2) || (selectionEndLine <=2)))
            {
                if (selectionEndLine>0)
                {
                    ::SendScintilla(SCI_GOTOLINE,selectionEndLine,0);
                } else
                {
                    ::SendScintilla(SCI_GOTOLINE,1,0);
                }
            }            
            // TODO: a more refine method to adjust the selection when for selection across 2 fields (one method is to adjust the start of selection no matter what, another approach is to look at the distance between the start/end point to the field boundary)
            //
            //if (selectionStartLine != selectionEndLine)
            //{
            //    if ((selectionStartLine <= 2) && (selectionEndLine > 2))
            //    {   
            //        ::SendMessage(curScintilla,SCI_SETSELECTIONSTART,::SendMessage(curScintilla,SCI_POSITIONFROMLINE,selectionEndLine,0),0);
            //    } else if ((selectionStartLine > 2) && (selectionEndLine <= 2))
            //    {
            //        ::SendMessage(curScintilla,SCI_SETSELECTIONEND,::SendMessage(curScintilla,SCI_GETLINEENDPOSITION,selectionStartLine,0),0);
            //    } else if ((selectionStartLine <= 2) && (selectionEndLine <= 2))
            //    {
            //        
            //    }
            //}
            //refreshAnnotation();
        }
        g_modifyResponse = true;
        g_selectionMonitor++;
        refreshAnnotation();  //TODO: consider only refresh annotation under some situation (for example only when an undo is done) to improve efficiency.
    }
}

void tagComplete()
{
    //TODO: can just use the snippetdock to do the completion (after rewrite of snippetdock)
    int posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
    if (triggerTag(posCurrent,true) > 0) snippetHintUpdate();
}

//TODO: better triggertag, should allow for a list of scopes
bool triggerTag(int &posCurrent,bool triggerTextComplete, int triggerLength)
{
    //HWND curScintilla = getCurrentScintilla();

    int paramPos = -1;
    if (triggerTextComplete == false)
    {
        paramPos = ::SendScintilla(SCI_BRACEMATCH,posCurrent-1,0);
        if ((paramPos>=0) && (paramPos<posCurrent))
        {
            triggerLength = triggerLength - (posCurrent - paramPos);
            posCurrent = paramPos;
            ::SendScintilla(SCI_GOTOPOS,paramPos,0);
            
        }
    }


    bool tagFound = false;
    char *tag;
	int tagLength = getCurrentTag(posCurrent, &tag, triggerLength);
    
    //int position = 0;
    //bool groupChecked = false;

    //int curLang = 0;
    //::SendMessage(nppData._nppHandle,NPPM_GETCURRENTLANGTYPE ,0,(LPARAM)&curLang);
    //wchar_t curLangNumber[10];
    //wchar_t curLangText[20];
    //::wcscpy(curLangText, TEXT("LANG_"));
    //::_itow_s(curLang, curLangNumber, 10, 10);
    //::wcscat(curLangText, curLangNumber);

    if (((triggerLength<=0) && (tag[0] == '_')) || (tagLength == 0))
    {
        delete [] tag;
    } else if (tagLength > 0) //TODO: changing this to >0 fixed the problem of tag_tab_completion, but need to investigate more about the side effect
	{
        
        int posBeforeTag = posCurrent - tagLength;

        char *expanded = NULL;
        char *tagType = NULL;
        
        TCHAR *fileType = NULL;
        fileType = new TCHAR[MAX_PATH];

        // Check for custom scope
        tagType = toCharArray(pc.configText[CUSTOM_SCOPE]);
        expanded = findTagSQLite(tag,tagType,triggerTextComplete); 
        
        // Check for snippets which matches ext part
        if (!expanded)
        {
            ::SendMessage(nppData._nppHandle, NPPM_GETNAMEPART, (WPARAM)MAX_PATH, (LPARAM)fileType);
            tagType = toCharArray(fileType);
            expanded = findTagSQLite(tag,tagType,triggerTextComplete); 
            
            // Check for snippets which matches name part
            if (!expanded)
            {
                ::SendMessage(nppData._nppHandle, NPPM_GETEXTPART, (WPARAM)MAX_PATH, (LPARAM)fileType);
                tagType = toCharArray(fileType);
                expanded = findTagSQLite(tag,tagType,triggerTextComplete); 
                // Check for language specific snippets
                if (!expanded)
                {
                    expanded = findTagSQLite(tag,getLangTagType(),triggerTextComplete); 
                    // TODO: Hardcode the extension associated with each language type, check whether the extension are the same as the current extenstion, if not, use findtagSQLite to search for snippets using those scopes
                    
                    // Check for snippets which matches the current language group
                    //if (!expanded)
                    //{
                    //    groupChecked = true;
                    //    position = 0;
                    //    do
                    //    {   
                    //        tagType = getGroupScope(curLangText,position);
                    //        if (tagType)
                    //        {
                    //            expanded = findTagSQLite(tag,tagType,triggerTextComplete); 
                    //            
                    //        } else
                    //        {
                    //            break;
                    //        }
                    //        position++;
                    //    } while (!expanded);
                    //}

                    // Check for GLOBAL snippets
                    if (!expanded)
                    {
                        //groupChecked = false;
                        expanded = findTagSQLite(tag,"GLOBAL",triggerTextComplete); 

                    }
                }
            }
        }
        
        // Only if a tag is found in the above process, a replace tag or trigger text completion action will be done.
        if (expanded)
        {
            if (triggerTextComplete)
            {
                ::SendScintilla(SCI_SETSEL,posBeforeTag,posCurrent);
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)expanded);
                posBeforeTag = posBeforeTag+strlen(expanded);
            } else
            {
                replaceTag(expanded, posCurrent, posBeforeTag);
            }
                
		    tagFound = true;
        }


        //int level=1;
        //do
        //{
        //    expanded = findTagSQLite(tag,level,triggerTextComplete); 
        //    
		//	if (expanded)
        //    {
        //        if (triggerTextComplete)
        //        {
        //            ::SendMessage(curScintilla,SCI_SETSEL,posBeforeTag,posCurrent);
        //            ::SendMessage(curScintilla,SCI_REPLACESEL,0,(LPARAM)expanded);
        //            posBeforeTag = posBeforeTag+strlen(expanded);
        //        } else
        //        {
        //            replaceTag(curScintilla, expanded, posCurrent, posBeforeTag);
        //        }
        //        
		//		tagFound = true;
        //        break;
        //    } 
        //    level++;
        //} while (level<=3);
        delete [] fileType;
        //if (!groupChecked) delete [] tagType;
        delete [] tagType;
        delete [] expanded;
		delete [] tag;
        
        // return to the original position 
        if (tagFound)
        {
            
            if (paramPos>=0)
            {

                int paramStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                int paramEnd = ::SendScintilla(SCI_BRACEMATCH,paramStart,0) + 1;
                
                //::SendScintilla(SCI_SETSELECTION,paramStart + 1,paramEnd - 1);
                //char* paramsContent = new char[paramEnd - 1 - (paramStart + 1) + 1];
                //::SendScintilla(SCI_GETSELTEXT,0, reinterpret_cast<LPARAM>(paramsContent));
                char* paramsContent;
                sciGetText(&paramsContent,paramStart+1,paramEnd-1);

                char paramsDelimiter = pc.configText[PARAMS_DELIMITER][0];
                g_hotspotParams = toVectorString(paramsContent,paramsDelimiter);
                ::SendScintilla(SCI_SETSELECTION,paramStart,paramEnd);
                ::SendScintilla(SCI_REPLACESEL,0,(LPARAM)"");
                delete [] paramsContent;
                
                //alertVector(g_hotspotParams);

                
            }
            ::SendScintilla(SCI_GOTOPOS,posBeforeTag,0);

        }
            
    } 
    return tagFound;
}

//bool snippetComplete()
//{
//    HWND curScintilla = getCurrentScintilla();
//    int posCurrent = ::SendMessage(curScintilla,SCI_GETCURRENTPOS,0,0);
//    bool tagFound = false;
//    char *tag;
//	int tagLength = getCurrentTag(curScintilla, posCurrent, &tag);
//    int posBeforeTag=posCurrent-tagLength;
//    if (tagLength != 0)
//	{
//        char *expanded;
//
//        int level=1;
//        do
//        {
//            expanded = findTagSQLite(tag,level,true); 
//			if (expanded)
//            {
//                ::SendMessage(curScintilla,SCI_SETSEL,posBeforeTag,posCurrent);
//                ::SendMessage(curScintilla,SCI_REPLACESEL,0,(LPARAM)expanded);
//                posBeforeTag = posBeforeTag+strlen(expanded);
//                
//                //replaceTag(curScintilla, expanded, posCurrent, posBeforeTag);
//				tagFound = true;
//                break;
//            } 
//            level++;
//        } while (level<=3);
//
//        delete [] expanded;
//		delete [] tag;
//        // return to the original path 
//        // ::SetCurrentDirectory(curPath);
//    }
//    // return to the original position 
//    if (tagFound) ::SendMessage(curScintilla,SCI_GOTOPOS,posBeforeTag,0);
//    return tagFound;
//}

void generateStroke(int vk, int modifier)
{
    //TODO: should be able to take an array of modifier
    if (modifier !=0) generateKey(modifier,true);
    generateKey(vk,true);
    generateKey(vk,false);
    if (modifier !=0) generateKey(modifier,false);
}

void generateKey(int vk, bool keyDown) 
{
    if (vk == 0) return;

    KEYBDINPUT kb = {0};
    INPUT input = {0};

    ZeroMemory(&kb, sizeof(KEYBDINPUT));
    ZeroMemory(&input, sizeof(INPUT));

    if (keyDown)
    {
        kb.dwFlags = 0;
    } else
    {
        kb.dwFlags = KEYEVENTF_KEYUP;
    }
    kb.dwFlags |= KEYEVENTF_EXTENDEDKEY;

    kb.wVk  = vk;
    input.type  = INPUT_KEYBOARD;
    input.ki  = kb;
    SendInput(1, &input, sizeof(input));
    ZeroMemory(&kb, sizeof(KEYBDINPUT));
    ZeroMemory(&input, sizeof(INPUT));
    return;
}

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    TCHAR title[1000];
    ZeroMemory(title, sizeof(title));
    GetWindowText(hwnd, title, sizeof(title)/sizeof(title[0]));
    if(_tcsstr(title, g_tempWindowKey))
    {
        g_tempWindowHandle = hwnd;
        return false;
    }
    return true;
}

void searchWindowByName(std::string searchKey, HWND parentWindow)
{
    if (searchKey == "")
    {
        g_tempWindowHandle = nppData._nppHandle;       
    } else
    {
        char* temp = new char [searchKey.size()+1];
        strcpy(temp, searchKey.c_str());
        g_tempWindowKey = toWideChar(temp);
        
        if (parentWindow != 0)
        {
            EnumChildWindows(parentWindow, enumWindowsProc, 0);
        } else
        {
            EnumWindows(enumWindowsProc, 0);
        }
        delete [] temp;
        delete [] g_tempWindowKey;
    }
}

void setFocusToWindow()
{
    
    SetActiveWindow(g_tempWindowHandle);
    SetForegroundWindow(g_tempWindowHandle);
    
}

char* getLangTagType()
{
    int curLang = 0;
    ::SendMessage(nppData._nppHandle,NPPM_GETCURRENTLANGTYPE ,0,(LPARAM)&curLang);
    //alertNumber(curLang);
    
    if ((curLang>54) || (curLang<0)) return "";

    //support the languages supported by npp 0.5.9, excluding "user defined language" abd "search results"
    char *s[] = {"Lang:TXT","Lang:PHP","Lang:C","Lang:CPP","Lang:CS","Lang:OBJC","Lang:JAVA","Lang:RC",
                 "Lang:HTML","Lang:XML","Lang:MAKEFILE","Lang:PASCAL","Lang:BATCH","Lang:INI","Lang:NFO","",
                 "Lang:ASP","Lang:SQL","Lang:VB","Lang:JS","Lang:CSS","Lang:PERL","Lang:PYTHON","Lang:LUA",
                 "Lang:TEX","Lang:FORTRAN","Lang:BASH","Lang:FLASH","Lang:NSIS","Lang:TCL","Lang:LISP","Lang:SCHEME",
                 "Lang:ASM","Lang:DIFF","Lang:PROPS","Lang:PS","Lang:RUBY","Lang:SMALLTALK","Lang:VHDL","Lang:KIX",
                 "Lang:AU3","Lang:CAML","Lang:ADA","Lang:VERILOG","Lang:MATLAB","Lang:HASKELL","Lang:INNO","",
                 "Lang:CMAKE","Lang:YAML","Lang:COBOL","Lang:GUI4CLI","Lang:D","Lang:POWERSHELL","Lang:R"};
    
    return s[curLang];
    //return "";
}



void httpToFile(TCHAR* server, TCHAR* request, TCHAR* requestType, TCHAR* pathWide)
{
    //TODO: should change the mouse cursor to waiting
    //TODO: should report error as a return value   
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    LPSTR pszOutBuffer;
    std::vector <std::string> vFileContent;
    BOOL  bResults = false;
    HINTERNET  hSession = NULL, 
               hConnect = NULL,
               hRequest = NULL;
    
    // Use WinHttpOpen to obtain a session handle.
    hSession = WinHttpOpen( L"WinHTTP",  
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, 
                            WINHTTP_NO_PROXY_BYPASS, 0);

    // Specify an HTTP server.
    if (hSession)
        hConnect = WinHttpConnect( hSession, server,
                                   INTERNET_DEFAULT_HTTP_PORT, 0);
    
    // Create an HTTP request handle.
    if (hConnect)
        hRequest = WinHttpOpenRequest( hConnect, requestType, request,
                                       NULL, WINHTTP_NO_REFERER, NULL, 
                                       NULL);
    
    // Send a request.
    if (hRequest)
        bResults = WinHttpSendRequest( hRequest,
                                       WINHTTP_NO_ADDITIONAL_HEADERS,
                                       0, WINHTTP_NO_REQUEST_DATA, 0, 
                                       0, 0);

    // End the request.
    if (bResults) bResults = WinHttpReceiveResponse( hRequest, NULL);

    char* path;
    FILE * pFile;
    //TODO: move this default value part to the snippet triggering function
    if (_tcslen(pathWide) <= 0)
    {
        
        path = toCharArray(g_fttempPath);
    } else
    {
        path = toCharArray(pathWide);
    }
    pFile = fopen(path, "w+b"); 
        
    if (bResults)
    {
        do 
        {
            // Check for available data.
            dwSize = 0;
            if (!WinHttpQueryDataAvailable( hRequest, &dwSize))
            {
                showMessageBox(TEXT("Error in WinHttpQueryDataAvailable."));
            }
    
            // Allocate space for the buffer.
            pszOutBuffer = new char[dwSize+1];
    
            if (!pszOutBuffer)
            {
                showMessageBox(TEXT("Out of memory."));
                dwSize=0;
            } else
            {
                // Read the Data.
                ZeroMemory(pszOutBuffer, dwSize+1);
    
                if (!WinHttpReadData( hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded))
                {
                    showMessageBox(TEXT("Error in WinHttpReadData."));
                } else
                {
                    fwrite(pszOutBuffer, (size_t)dwDownloaded, (size_t)1, pFile);
                }
    
                // Free the memory allocated to the buffer.
                delete [] pszOutBuffer;
            }
        } while (dwSize>0);
    }
    fclose (pFile);

    delete [] path;

    // Report any errors.
    if (!bResults) showMessageBox(TEXT("Error has occurred."));
    // Close any open handles.
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
}



std::vector<std::string> smartSplit(int start, int end, char delimiter, int parts)
{
    char filler;
    if (delimiter!=0)
    {
        filler = '1';
    } else 
    {
        filler = '0';
    }

    std::vector<std::string> retVal;
    std::vector<int> positions;
    char* partToSplit;
    sciGetText(&partToSplit, start, end);
    int signSpot; 
    int tailSpot;
    char* tagSignGet;
    char* tagTailGet;
    
    SendScintilla(SCI_GOTOPOS,start,0);
    do
    {
        signSpot = searchNext("\\$\\[.\\[",true);
        if ((signSpot>=start) && (signSpot < end))
        {
            
            sciGetText(&tagSignGet);
            tagTailGet = new char[4];
            strcpy(tagTailGet,"]!]");
            tagTailGet[1] = tagSignGet[2];
            SendScintilla(SCI_GOTOPOS,signSpot+1,0);
            //searchNextMatchedTail(tagSignGet,tagTailGet);
            //tailSpot = SendScintilla(SCI_GETCURRENTPOS,0,0);
            tailSpot = searchNextMatchedTail(tagSignGet,tagTailGet);
            if (tailSpot <= end && tailSpot> start)
            {
                for (int i = signSpot - start; i<tailSpot-start;i++) partToSplit[i] = filler;
            }
            delete [] tagSignGet;
            delete [] tagTailGet;
        }
    } while ((signSpot < end) && (signSpot > start));

    //alert(partToSplit);
    
    retVal = toVectorString(partToSplit,delimiter,parts);
    
    int i = 0;
    for (i = 0; i<retVal.size();i++)
    {
        positions.push_back(retVal[i].length());
    }

    int caret = start;
    char* tempString;
    for (i = 0; i<positions.size();i++)
    {
        //alertNumber(positions[i]);
        sciGetText(&tempString, caret, caret + positions[i]);
        retVal[i] = toString(tempString);
        delete [] tempString;
        caret += positions[i]+1;
    }
    delete [] partToSplit;
    return retVal;
}



void tabActivate()
{

    //HWND curScintilla = getCurrentScintilla();

    //if ((g_enable==false) || (::SendScintilla(SCI_SELECTIONISRECTANGLE,0,0)==1))
    if ((g_enable==false) || (g_rectSelection==true))
    {        
        ::SendScintilla(SCI_TAB,0,0);   
    } else
    {
        int posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
        //int posTriggerStart = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
        int lineCurrent = ::SendScintilla(SCI_LINEFROMPOSITION,posCurrent,0);

        if ((g_editorView == true) && (lineCurrent <=2))
        {
            if (lineCurrent == 1)
            {
                //TODO: can make the Tab select the whole field instead of just GOTOLINE
                //::SendMessage(curScintilla,SCI_SETSEL,::SendMessage(curScintilla,SCI_POSITIONFROMLINE,2,0),::SendMessage(curScintilla,SCI_GETLINEENDPOSITION,2,0));
                ::SendScintilla(SCI_GOTOLINE,2,0);
            } else if (lineCurrent == 2)
            {
                ::SendScintilla(SCI_GOTOLINE,3,0);
            }

        } else
        {


        //bool optionTriggered = false;
        //if (g_optionMode == true)
        //{
        //    if (posCurrent == g_optionStartPosition)
        //    {
        //        optionNavigate(curScintilla);
        //        optionTriggered = true;
        //        g_optionMode = true; // TODO: investigate why this line is necessary
        //    } else
        //    {
        //        cleanOptionItem();
        //        g_optionMode = false;
        //    }
        //}
        //
        //if (optionTriggered == false)

        //if (g_optionMode == true)
        //{
        //    g_optionMode = false;
        //    ::SendMessage(curScintilla,SCI_GOTOPOS,g_optionEndPosition,0);
        //    snippetHintUpdate();
        //} else
        //{
            g_hotspotParams.clear();
            
            pc.configInt[LIVE_HINT_UPDATE]--;
            g_selectionMonitor--;
            
            int posSelectionStart = ::SendScintilla(SCI_GETSELECTIONSTART,0,0);
            int posSelectionEnd = ::SendScintilla(SCI_GETSELECTIONEND,0,0);

            if (pc.configInt[PRESERVE_STEPS]==0) ::SendScintilla(SCI_BEGINUNDOACTION, 0, 0);
            bool tagFound = false;
            if (posSelectionStart==posSelectionEnd)
            {
                tagFound = triggerTag(posCurrent);
            }

            if (tagFound)
            {
                ::SendScintilla(SCI_AUTOCCANCEL,0,0);
                posCurrent = ::SendScintilla(SCI_GETCURRENTPOS,0,0);
                g_lastTriggerPosition = posCurrent;
            } 
            

            int navSpot = 0;
            //bool dynamicSpot0 = false;
            //bool dynamicSpot1 = false;
            //bool dynamicSpot2 = false;
            bool dynamicSpotTemp = false;
            bool dynamicSpot = false;

            if (g_editorView == false)
            {
                int i;
                if (!tagFound) 
                {
                    i = g_listLength-1;
                    if (posCurrent > g_lastTriggerPosition)
                    {
                        do
                        {
                            //TODO: limit the search to g_lastTriggerPosition ?       
                            if (searchPrev(g_tagSignList[i]) >= 0)
                            {
                                ::SendScintilla(SCI_GOTOPOS,g_lastTriggerPosition,0);
                                posCurrent = g_lastTriggerPosition;
                                break;   
                            }
                            i--;
                        } while (i>=0);
                    }

                 
                    //if (searchNext("$[2[") < 0)
                    //{
                    //    if (searchNext("$[1[")<0)
                    //    {
                    //        if (searchNext("$[![")<0)
                    //        {
                                //if ((searchPrev("$[2[") >= 0) || (searchPrev("$[1[")>=0) || (searchPrev("$[![")>=0))
                                //{
                                //    ::SendScintilla(SCI_GOTOPOS,g_lastTriggerPosition,0);
                                //    posCurrent = g_lastTriggerPosition;
                                //}
                    //       }
                    //   }
                    //}
                }
                //TODO: cater more level of priority
                //TODO: Params inertion will stop when navSpot is true, so it is not working properly under differnt level of priority
                //      Or in other words it only work for the highest existing level of priority
                i = g_listLength - 1;
                
                do
                {
                    if (dynamicSpot)
                    {
                        dynamicHotspot(posCurrent,g_tagSignList[i],g_tagTailList[i]);
                    } else
                    {
                        dynamicSpot = dynamicHotspot(posCurrent,g_tagSignList[i],g_tagTailList[i]);
                    }
                    ::SendScintilla(SCI_GOTOPOS,posCurrent,0);
                    
                    navSpot = hotSpotNavigation(g_tagSignList[i],g_tagTailList[i]);
                    
                    i--;
                } while ((navSpot <= 0) && (i >= 0));

                
                //dynamicSpot = dynamicHotspot(posCurrent,"$[2[","]2]");
                //::SendScintilla(SCI_GOTOPOS,posCurrent,0);
                //navSpot = hotSpotNavigation("$[2[","]2]");
                //
                //if (navSpot == false)
                //{
                //    if (dynamicSpot)
                //    {
                //        dynamicHotspot(posCurrent,"$[1[","]1]");
                //    } else
                //    {
                //        dynamicSpot = dynamicHotspot(posCurrent,"$[1[","]1]");
                //    }
                //    ::SendScintilla(SCI_GOTOPOS,posCurrent,0);
                //    navSpot = hotSpotNavigation("$[1[","]1]");
                //}
                //if (navSpot == false)
                //{
                //    if (dynamicSpot)
                //    {
                //        dynamicHotspot(posCurrent); //TODO: May still consider do some checking before going into dynamic hotspot for performance improvement
                //    } else
                //    {
                //        dynamicSpot = dynamicHotspot(posCurrent);
                //    }
                //    ::SendScintilla(SCI_GOTOPOS,posCurrent,0);
                //    navSpot = hotSpotNavigation();
                //}

                //if ((dynamicSpot2) || (dynamicSpot1) || (dynamicSpot0)) dynamicSpot = true;
                
                //TODO: this line is position here so the priority spot can be implement, but this cause the 
                //      1st hotspot not undoable when the snippet is triggered. More investigation on how to
                //      manipulate the undo list is required to make these 2 features compatible
                if (pc.configInt[PRESERVE_STEPS]==0) ::SendScintilla(SCI_ENDUNDOACTION, 0, 0);

                if (navSpot != 3)
                {
                    if ((navSpot > 0) || (dynamicSpot)) ::SendScintilla(SCI_AUTOCCANCEL,0,0);
                }
                //}
            } else
            {
                if (pc.configInt[PRESERVE_STEPS]==0) ::SendScintilla(SCI_ENDUNDOACTION, 0, 0);
            }

            bool snippetHint = false;

            bool completeFound = false;
            if (pc.configInt[TAB_TAG_COMPLETION] == 1)
            {
                if ((navSpot == 0) && (tagFound == false) && (dynamicSpot==false)) 
	    	    {
                    
                    ::SendScintilla(SCI_GOTOPOS, posSelectionStart, 0);
                    posCurrent = posSelectionStart;
                    //completeFound = snippetComplete();
                    completeFound = triggerTag(posCurrent,true);
                    if (completeFound)
                    {
                        ::SendScintilla(SCI_AUTOCCANCEL,0,0);
                        snippetHint = true;
                    }
	    	    }
            }
            
            if ((navSpot == 0) && (tagFound == false) && (completeFound==false) && (dynamicSpot==false)) 
            {
                if (g_optionMode == true)
                {
                    //g_optionMode = false;
                    turnOffOptionMode();
                    ::SendScintilla(SCI_GOTOPOS,g_optionEndPosition,0);
                    snippetHint = true;
                } else
                {
                    //g_tempWindowHandle = (HWND)::SendMessage(nppData._nppHandle,NPPM_DMMGETPLUGINHWNDBYNAME ,(WPARAM)TEXT("SherloXplorer"),(LPARAM)TEXT("SherloXplorer.dll"));
                    //setFocusToWindow();
                    //generateKey(toVk("TAB"),true);
                    //generateKey(toVk("TAB"),false);
                    restoreTab(posCurrent, posSelectionStart, posSelectionEnd);
                }
            }

            pc.configInt[LIVE_HINT_UPDATE]++;
            if (snippetHint) snippetHintUpdate();
            g_selectionMonitor++;
        //}
        }
    }
}



void testThread( void* pParams )
{ 
    //system("npp -multiInst");
    for (int i = 0; i<10;i++)
    {
        SendScintilla(SCI_REPLACESEL,0,(LPARAM)"ABC");
        Sleep(1000);
    }
    
}

void testing2()
{
    alert("testing2");


}

void testing()
{
    alert("testing1");

    alert(pc.configText[2]);
    alert(pc.configText[1]);
    alert(pc.configText[CUSTOM_SCOPE]);
    //// Testing thread
    //_beginthread( testThread, 0, NULL );

    //// Testing sorting
    //std::vector<std::string> test = toVectorString("BAR|DOOR|CAT_1|KING|CAT|CUP",'|');
    //alert(test);
    //alert(toSortedVectorString(test));
    //

    //// Testing alert()
    //alert();
    //alert(5);
    //alert(0.375);
    //alert('I');
    //alert("Hello");
    //alert(TEXT("World"));
    //std::string testing = "FOO";
    //alert(testing);
    //std::vector<std::string> testing2 = toVectorString("BAR|DOOR|CAT_1|KING|CAT|CUP",'|');
    //alert(testing2);
    //std::vector<int> testing3;
    //testing3.push_back(3);
    //testing3.push_back(2);
    //testing3.push_back(1);
    //alert(testing3);
    //alert(nppData._nppHandle);
    //
    //

    //// Testing disable item
    // HMENU hMenu = (HMENU)::SendMessage(nppData._nppHandle, NPPM_GETMENUHANDLE, 0, 0);
    //::EnableMenuItem(hMenu, funcItem[0]._cmdID, MF_BYCOMMAND | (false?0:MF_GRAYED));
    //::ModifyMenu(hMenu, funcItem[1]._cmdID, MF_BYCOMMAND | MF_SEPARATOR, 0, 0);

    ////Manipulate windows handle
    //char buffer [100];
    //sprintf(buffer, "0x%08x", (unsigned __int64) g_tempWindowHandle);
    //alertCharArray(buffer);

    //long handle = reinterpret_cast<long>(g_tempWindowHandle);
    //alertNumber(handle);
    //HWND newWin = reinterpret_cast<HWND>(handle);


    ////Test regexp
    //alertNumber(searchNext("\\$\\[.\\[",true));


    

    ////Testing SchintillaGetText
    //char* temp;
    //sciGetText(&temp,10,20);
    //alertCharArray(temp);
    //delete [] temp;
    //char* temp2;
    //sciGetText(&temp2,13,13);
    //alertCharArray(temp2);
    //delete [] temp2;


    ////Test calltip
    //::SendScintilla(SCI_CALLTIPSHOW,0,(LPARAM)"Hello World!");


    //// test getting windows by enum windows
    //searchWindowByName("RGui");
    //searchWindowByName("R Console",g_tempWindowHandle);
    //setFocusToWindow();
    //::generateKey(toVk("CONTROL"), true);
    //::generateStroke(toVk("V"));
    //::generateKey(toVk("CONTROL"), false);
    //::generateStroke(VK_RETURN);
    
    //HWND tempWindowHandle = ::FindWindowEx(g_tempWindowHandle, 0, TEXT("Edit"),0);

     

    ////testing generatekey and get other windows
    //HWND hwnd;
    //hwnd = FindWindow(NULL,TEXT("RGui"));
    //
    //
    //SetActiveWindow(hwnd);
    //SetForegroundWindow(hwnd);
    //
    //::GenerateKey(VK_CONTROL, true);
    //::GenerateKey(0x56, true);
    //::GenerateKey(0x56, false);
    //::GenerateKey(VK_CONTROL, false);
    //
    ////::Sleep(1000);
    //::GenerateKey(VK_RETURN, true);
    //::GenerateKey(VK_RETURN, false);
    //::Sleep(1000);
    //
    //SetActiveWindow(nppData._nppHandle);
    //SetForegroundWindow(nppData._nppHandle);


    ////Testing Find and replace
    //std::string str1 = "abcdecdf";
    //std::string str2 = "cd";
    //std::string str3 = "ghicdjk";
    //alertString(str1);
    //alertString(str2);
    //alertString(str3);
    //
    //findAndReplace(str1,str2,str3);
    //alertString(str1);

    ////Testing vector<string>
    //int i;
    //char* teststr = new char[100];
    //strcpy(teststr, "abc def  ghi");
    //alertCharArray(teststr);
    //
    //std::vector<std::string> v;
    //    
    //v = toVectorString(teststr,' ');
    //delete [] teststr;
    //i = 0;
    //while (i<v.size())
    //{
    //    alertString(v[i]);
    //    i++;
    //}


    ////Testing brace match
    //int result = ::SendScintilla(SCI_BRACEMATCH,3,0);
    //alertNumber(result);


    //// For opening static dialog
    //openDummyStaticDlg();

    
    //Testing usage of quickStrip()
    //char* test = new char[MAX_PATH];
    //strcpy(test,"To test the quickStrip.");
    //alertCharArray(test);
    //test = quickStrip(test,'t');
    //alertCharArray(test);
    //delete [] test;
    //alertCharArray(test);

    //cleanOptionItem();
            
    //testing add and get optionitem, testing for memory leak
    //char* optionText;
    //optionText = new char[1000];
    //strcpy(optionText,"abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc");
    //addOptionItem(optionText);
    //char* optionText2;
    //optionText2 = new char[1000];
    //strcpy(optionText2,"defdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdefdef");
    //addOptionItem(optionText2);
    //char* optionText3;
    //optionText3 = new char[1000];
    //strcpy(optionText3,"ghighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighighi");
    //addOptionItem(optionText3);
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //cleanOptionItem();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //alertCharArray(g_optionArray[g_optionCurrent]);
    //updateOptionCurrent();
    //cleanOptionItem();

    //test using the getDateTime function
    //char* date = getDateTime("yyyyMMdd");
    //alertCharArray(date);
    //delete [] date;

    // testing to upper and to lower
    //char* str = new char[200];
    //strcpy(str,"aBcDe");
    //alertCharArray(str);
    //alertCharArray(::strupr(str));
    //alertCharArray(::strlwr(str));
    //delete [] str;


    //alertNumber(pc.snippetListLength);
    //alertCharArray(getLangTagType());

    //char* testScope = NULL;
    //testScope = getGroupScope(TEXT("LANG_4"),0);
    //
    //alertCharArray(testScope);
    //testScope = getGroupScope(TEXT("LANG_4"),1);
    //alertCharArray(testScope);
    //testScope = getGroupScope(TEXT("LANG_4"),2);
    //alertCharArray(testScope);
    //testScope = getGroupScope(TEXT("LANG_4"),3);
    //if (testScope) alertCharArray(testScope);
    //if (testScope) delete [] testScope;

    //Test current language check
    //int curLang = 0;
    //::SendMessage(nppData._nppHandle,NPPM_GETCURRENTLANGTYPE ,0,(LPARAM)&curLang);
    //
    //alertNumber(curLang);

    

    //pc.configText[CUSTOM_SCOPE] = TEXT(".cpp");
    //saveCustomScope();

    
    // Testing array of char array
    //char *s[] = {"Jan","Feb","Mar","April","May"};
    //char* a = "December";
    //s[1] = a;
    //alertCharArray(s[0]);
    //alertCharArray(s[1]);
    //alertCharArray(s[2]);
    //alertCharArray(s[3]);
    //
    //alertNumber(sizeof(s));
    //alertNumber(sizeof*(s));

    //char* b = "July";
    //s[1] = b;
    //alertCharArray(s[0]);
    //alertCharArray(s[1]);
    //alertCharArray(s[2]);
    //alertCharArray(s[3]);
    //
    //
    //g_optionArray[0] = "abc";
    //alertCharArray(g_optionArray[0]);
    //alertCharArray(g_optionArray[1]);
    //alertCharArray(g_optionArray[2]);
    //alertCharArray(g_optionArray[3]);
    //alertCharArray(g_optionArray[4]);
    
    // Testing array of char array
    //const char *s[]={"Jan","Feb","Mar","April"};
    //const char **p;
    //size_t i;
    //
    //#define countof(X) ( (size_t) ( sizeof(X)/sizeof*(X) ) )
    //
    //alertCharArray("Loop 1:");
    //for (i = 0; i < countof(s); i++)
    //alertCharArray((char*)(s[i]));
    //
    //alertCharArray("Loop 2:");
    //for (p = s, i = 0; i < countof(s); i++)
    //alertCharArray((char*)(p[i]));
    //
    //alertCharArray("Loop 3:");
    //for (p = s; p < &s[countof(s)]; p++)
    //alertCharArray((char*)(*p));
    

    //setCommand(TRIGGER_SNIPPET_INDEX, TEXT("Trigger Snippet/Navigate to Hotspot"), fingerText, NULL, false);
    //::GenerateKey(VK_TAB, TRUE);

    //ShortcutKey *shKey = new ShortcutKey;
	//shKey->_isAlt = true;
	//shKey->_isCtrl = true;
	//shKey->_isShift = true;
	//shKey->_key = VK_TAB;
    //setCommand(TRIGGER_SNIPPET_INDEX, TEXT("Trigger Snippet/Navigate to Hotspot"), fingerText, shKey, false);
    // create process, no console window
    //STARTUPINFO         si;
    //PROCESS_INFORMATION pi;
    ////TCHAR               cmdLine[MAX_PATH] = L"npp -multiInst";
    //TCHAR               cmdLine[MAX_PATH] = L"dir > d:\\temp.txt";
    //
    //::ZeroMemory(&si, sizeof(si));
    //si.cb = sizeof(si);
    //
    //::CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    
    
    //Console::ReadLine();


    

    //::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
    //selectionToSnippet();

    //int posCurrent = ::SendMessage(curScintilla,SCI_GETCURRENTPOS,0,0);

    //snippetComplete(posCurrent);
    

    //
    //if (newUpdate)
    //{
    //    ::MessageBox(nppData._nppHandle, TEXT("New!"), TEXT(PLUGIN_NAME), MB_OK);
    //} else
    //{
    //    ::MessageBox(nppData._nppHandle, TEXT("Old!"), TEXT(PLUGIN_NAME), MB_OK);
    //}
    //
    //
    //::SendMessage(curScintilla, SCI_SETLINEINDENTATION, 0, 11);
    //::SendMessage(curScintilla, SCI_SETLINEINDENTATION, 1, 11);
    //::SendMessage(curScintilla, SCI_SETLINEINDENTATION, 2, 11);
    //::SendMessage(curScintilla, SCI_SETLINEINDENTATION, 3, 11);

    //int enc = ::SendMessage(curScintilla, SCI_GETLINEINDENTATION, 0, 0);
    //wchar_t countText[10];
    //::_itow_s(enc, countText, 10, 10); 
    //::MessageBox(nppData._nppHandle, countText, TEXT(PLUGIN_NAME), MB_OK);

    //    
    ////char *tagType1 = NULL;
    //TCHAR fileType1[5];
    //::SendMessage(nppData._nppHandle, NPPM_GETEXTPART, (WPARAM)MAX_PATH, (LPARAM)fileType1);
    ////toCharArray(fileType1, &tagType1);
    //::MessageBox(nppData._nppHandle, fileType1, TEXT(PLUGIN_NAME), MB_OK);
    //
    //TCHAR key[MAX_PATH];
    //::swprintf(key,fileType1);
    //::MessageBox(nppData._nppHandle, key, TEXT(PLUGIN_NAME), MB_OK);
    //
    //const TCHAR* key2 = (TCHAR*)".txt";
    //
    //if (key==key2)
    //{
    //    ::MessageBox(nppData._nppHandle, TEXT("txt!"), TEXT(PLUGIN_NAME), MB_OK);
    //
    //} else
    //{
    //   ::MessageBox(nppData._nppHandle, TEXT("not txt!"), TEXT(PLUGIN_NAME), MB_OK);
    //}
    //
    //::SendMessage(curScintilla, SCI_ANNOTATIONSETTEXT, 0, (LPARAM)"Hello!");
    //::SendMessage(curScintilla, SCI_ANNOTATIONSETVISIBLE, 2, 0);
      
    //exportSnippets();

    // messagebox shows the current buffer encoding id
    //int enc = ::SendMessage(nppData._nppHandle, NPPM_GETBUFFERENCODING, (LPARAM)::SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0), 0);

    //int enc = pc.snippetListLength;
    //wchar_t countText[10];
    //::_itow_s(enc, countText, 10, 10); 
    //::MessageBox(nppData._nppHandle, countText, TEXT(PLUGIN_NAME), MB_OK);


    //TCHAR file2switch[]=TEXT("C:\\Users\\tomtom\\Desktop\\FingerTextEditor");
    //TCHAR file2switch[]=TEXT("FingerTextEditor");
    //::SendMessage(nppData._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)file2switch);

    //::SendMessage(nppData._nppHandle, NPPM_ACTIVATEDOC, MAIN_VIEW, 1);
}
