/**********************************************************************

   Audacity - A Digital Audio Editor
   Copyright 1999-2009 Audacity Team
   License: wxwidgets

   Dan Horgan
   James Crook

******************************************************************//**

\file SetTrackInfoCommand.cpp
\brief Definitions for SetTrackInfoCommand and SetTrackInfoCommandType classes

\class SetTrackInfoCommand
\brief Command that sets track information , name, mute/sol etc.

*//*******************************************************************/

#include "../Audacity.h"
#include "SetTrackInfoCommand.h"
#include "../Project.h"
#include "../Track.h"
#include "../TrackPanel.h"
#include "../WaveTrack.h"
#include "../ShuttleGui.h"
#include "CommandContext.h"

SetTrackInfoCommand::SetTrackInfoCommand()
{
   mTrackIndex = 0;
   mTrackName = "unnamed";
   mPan = 0.0f;
   mGain = 1.0f;
   bSelected = false;
   bFocused = false;
   bSolo = false;
   bMute = false;
}

bool SetTrackInfoCommand::DefineParams( ShuttleParams & S ){ 
   S.Define(   mTrackIndex,                        wxT("TrackIndex"), 0, 0, 100 );
   S.Optional( bHasTrackName ).Define( mTrackName, wxT("Name"),       wxT("Unnamed") );
   S.Optional( bHasPan       ).Define( mPan,       wxT("Pan"),        0.0, -1.0, 1.0);
   S.Optional( bHasGain      ).Define( mGain,      wxT("Gain"),       1.0,  0.0, 10.0);
   S.Optional( bHasSelected  ).Define( bSelected,  wxT("Selected"),   false );
   S.Optional( bHasFocused   ).Define( bFocused,   wxT("Focuseed"),   false );
   S.Optional( bHasSolo      ).Define( bSolo,      wxT("Solo"),       false );
   S.Optional( bHasMute      ).Define( bMute,      wxT("Mute"),       false );
   return true;
};

void SetTrackInfoCommand::PopulateOrExchange(ShuttleGui & S)
{
   S.AddSpace(0, 5);

   S.StartMultiColumn(2, wxALIGN_CENTER);
   {
      S.TieNumericTextBox( _("Track Index"), mTrackIndex );
   }
   S.EndMultiColumn();
   S.StartMultiColumn(3, wxALIGN_CENTER);
   {
      S.Optional( bHasTrackName ).TieTextBox(        _("Name"),        mTrackName );
      S.Optional( bHasPan       ).TieSlider(         _("Pan"),         mPan,  1.0, -1.0);
      S.Optional( bHasGain      ).TieSlider(         _("Gain"),        mGain, 10.0, 0.0);
   }
   S.EndMultiColumn();
   S.StartMultiColumn(2, wxALIGN_CENTER);
   {
      S.Optional( bHasSelected  ).TieCheckBox(       _("Selected"),    bSelected );
      S.Optional( bHasFocused   ).TieCheckBox(       _("Focused"),     bFocused);
      S.Optional( bHasSolo      ).TieCheckBox(       _("Solo"),        bSolo);
      S.Optional( bHasMute      ).TieCheckBox(       _("Mute"),        bMute);
   }
   S.EndMultiColumn();
}

bool SetTrackInfoCommand::Apply(const CommandContext & context)
{
   //wxString mode = GetString(wxT("Type"));

   // (Note: track selection ought to be somewhere else)
   long i = 0;
   TrackListIterator iter(context.GetProject()->GetTracks());
   Track *t = iter.First();
   while (t && i != mTrackIndex)
   {
      t = iter.Next();
      ++i;
   }
   if (i != mTrackIndex || !t)
   {
      context.Error(wxT("TrackIndex was invalid."));
      return false;
   }

   auto wt = dynamic_cast<WaveTrack *>(t);
   auto pt = dynamic_cast<PlayableTrack *>(t);

   if( bHasTrackName )
      t->SetName(mTrackName);
   if( wt && bHasPan )
      wt->SetPan(mPan);
   if( wt && bHasGain )
      wt->SetGain(mGain);
   if( bHasSelected )
      t->SetSelected(bSelected);
   if( bHasFocused )
   {
      TrackPanel *panel = context.GetProject()->GetTrackPanel();
      panel->SetFocusedTrack( t );
   }
   if( pt && bHasSolo )
      pt->SetSolo(bSolo);
   if( pt && bHasMute )
      pt->SetMute(bMute);

   return true;
}
