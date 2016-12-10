/*
 * Copyright (c) 1997 - 2001 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic license.
 * (see license.txt)
 */

#include <stdio.h>

#include "../simdebug.h"
#include "../simworld.h"
#include "../display/simimg.h"

#include "../utils/simrandom.h"
#include "../boden/grund.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/environment.h"

#include "simpeople.h"
#include "../besch/fussgaenger_besch.h"

static uint32 const strecke[] = { 6000, 11000, 15000, 20000, 25000, 30000, 35000, 40000 };

static weighted_vector_tpl<const fussgaenger_besch_t*> liste; // All pedestrians
static weighted_vector_tpl<const fussgaenger_besch_t*> current_pedestrians; // Only those allowed on the current timeline
stringhashtable_tpl<const fussgaenger_besch_t *> pedestrian_t::table;


static bool compare_fussgaenger_besch(const fussgaenger_besch_t* a, const fussgaenger_besch_t* b)
{
	// sort pedestrian objects descriptors by their name
	return strcmp(a->get_name(), b->get_name())<0;
}


bool pedestrian_t::register_besch(const fussgaenger_besch_t *besch)
{
	if(  table.remove(besch->get_name())  ) {
		dbg->warning( "fussgaenger_besch_t::register_besch()", "Object %s was overlaid by addon!", besch->get_name() );
	}
	table.put(besch->get_name(), besch);
	return true;
}


bool pedestrian_t::alles_geladen()
{
	liste.resize(table.get_count());
	if (table.empty()) {
		DBG_MESSAGE("pedestrian_t", "No pedestrians found - feature disabled");
	}
	else {
		vector_tpl<const fussgaenger_besch_t*> temp_liste(0);
		FOR(stringhashtable_tpl<fussgaenger_besch_t const*>, const& i, table) {
			// just entered them sorted
			temp_liste.insert_ordered(i.value, compare_fussgaenger_besch);
		}
		FOR(vector_tpl<fussgaenger_besch_t const*>, const i, temp_liste) {
			liste.append(i, i->get_gewichtung());
		}
	}
	return true;
}


pedestrian_t::pedestrian_t(loadsave_t *file)
#ifdef INLINE_OBJ_TYPE
 : road_user_t(pedestrian)
#else
 : road_user_t()
#endif
{
	rdwr(file);
	if(besch) {
		welt->sync.add(this);
	}
}


pedestrian_t::pedestrian_t(grund_t *gr, uint32 time_to_live) :
#ifdef INLINE_OBJ_TYPE
	road_user_t(pedestrian, gr, simrand(65535, "pedestrian_t::pedestrian_t (weg_next)")),
#else
	road_user_t(gr, simrand(65535, "pedestrian_t::pedestrian_t (weg_next)")),
#endif
	besch(pick_any_weighted(current_pedestrians))
{
	time_to_life = time_to_live == 0 ? pick_any(strecke) : time_to_live;
	calc_image();
}


pedestrian_t::~pedestrian_t()
{
	if(  time_to_life>0  ) {
		welt->sync.remove( this );
	}
}


void pedestrian_t::calc_image()
{
	if(!besch)
	{
		time_to_life = 0;
	}
	else
	{
		set_bild(besch->get_bild_nr(ribi_t::get_dir(get_direction())));
	}
}



void pedestrian_t::rdwr(loadsave_t *file)
{
	xml_tag_t f( file, "pedestrian_t" );

	road_user_t::rdwr(file);

	if(!file->is_loading()) {
		const char *s = besch->get_name();
		file->rdwr_str(s);
	}
	else {
		char s[256];
		file->rdwr_str(s, lengthof(s));
		besch = table.get(s);
		// unknown pedestrian => create random new one
		if(besch == NULL  &&  !liste.empty()  ) {
			besch = pick_any_weighted(liste);
		}
	}

	if(file->get_version()<89004) {
		time_to_life = pick_any(strecke);
	}
}



// create a number (anzahl) of pedestrians (if possible)
void pedestrian_t::generate_pedestrians_at(const koord3d k, uint32 anzahl, uint32 time_to_live)
{
#ifdef FORBID_SYNC_OBJECTS
	return;
#endif
#ifdef FORBID_PEDESTRIANS
	return;
#endif
	if (current_pedestrians.empty()) 
	{
		return;
	}

	grund_t* gr = welt->lookup(k);
	if (gr)
	{
		weg_t* weg = gr->get_weg(road_wt);

		if (!weg)
		{
			for (int i = 0; i < 8; i++)
			{
				grund_t *gr2 = world()->lookup_kartenboden(k.get_2d() + koord::neighbours[i]);
				weg = gr2 ? gr2->get_weg(road_wt) : NULL;
				if (weg)
				{
					break;
				}
			}
		}

		if (!weg)
		{
			return;
		}

		
		for (int i = 0; i < anzahl; i++)
		{
			pedestrian_t* ped = new pedestrian_t(gr, time_to_live);
			ped->calc_height(gr);
#ifndef MULTI_THREAD
			bool ok = gr->obj_add(ped) != 0;	// 256 limit reached
			// ok == false is quite frequent here.
			if (ok)
			{				
				if (i > 0)
				{
					// walk a little
					ped->sync_step((i & 3) * 64 * 24);
				}
#endif
#ifdef MULTI_THREAD
				karte_t::pedestrians_added_threaded[karte_t::passenger_generation_thread_number].append(ped);
#else
				welt->sync.add(ped);
			}
			else
			{
				// delete it, if we could not put it on the map
				ped->set_flag(obj_t::not_on_map);
				// do not try to delete it from sync-list
				ped->time_to_life = 0;
				delete ped;
				return; // it is pointless to try again
			}
#endif
		}
	}
}


sync_result pedestrian_t::sync_step(uint32 delta_t)
{
	time_to_life -= delta_t;

	if (time_to_life>0) {
		weg_next += 128*delta_t;
		weg_next -= do_drive( weg_next );
		return time_to_life>0 ? SYNC_OK : SYNC_DELETE;
	}
	return SYNC_DELETE;
}


grund_t* pedestrian_t::hop_check()
{
	grund_t *from = welt->lookup(pos_next);
	if(!from) {
		time_to_life = 0;
		return NULL;
	}

	// find the allowed directions
	const weg_t *weg = from->get_weg(road_wt);
	if(weg==NULL) {
		// no road anymore: destroy it
		time_to_life = 0;
		return NULL;
	}
	return from;
}


void pedestrian_t::hop(grund_t *gr)
{
	leave_tile();
	set_pos(gr->get_pos());
	calc_image();
	// no need to call enter_tile();
	gr->obj_add(this);

	const weg_t *weg = gr->get_weg(road_wt);
	// new target
	grund_t *to = NULL;
	// ribi opposite to current direction
	ribi_t::ribi gegenrichtung = ribi_t::rueckwaerts( get_direction() );
	// all possible directions
	ribi_t::ribi ribi = weg->get_ribi_unmasked() & (~gegenrichtung);
	// randomized offset
	const uint8 offset = (ribi > 0  &&  ribi_t::ist_einfach(ribi)) ? 0 : simrand(4, "void pedestrian_t::hop(grund_t *gr)");

	for(uint r = 0; r < 4; r++) {
		ribi_t::ribi const test_ribi = ribi_t::nsow[ (r+offset) & 3];

		if(  (ribi & test_ribi)!=0  &&  gr->get_neighbour(to, road_wt, test_ribi) )	{
			// this is our next target
			break;
		}
	}

	if (to) {
		pos_next = to->get_pos();
		direction = calc_set_direction(get_pos(), pos_next);
	}
	else {
		// turn around
		direction = gegenrichtung;
		dx = -dx;
		dy = -dy;
		pos_next = get_pos();
		// .. but this looks ugly, so disappear
		time_to_life = 0;
	}
}

void pedestrian_t::check_timeline_pedestrians()
{
	current_pedestrians.clear();
	FOR(weighted_vector_tpl<const fussgaenger_besch_t*>, fb, liste)
	{
		if (fb->is_available(world()->get_timeline_year_month()))
		{
			current_pedestrians.append(fb, fb->get_gewichtung()); 
		}
	}
}