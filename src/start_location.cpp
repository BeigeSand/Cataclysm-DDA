#include "start_location.h"
#include "output.h"
#include "debug.h"
#include "map.h"
#include "game.h"
#include "overmapbuffer.h"

static location_map _locations;

start_location::start_location()
{
    _ident = "";
    _name = "null";
    _target = "shelter";
}

start_location::start_location( std::string ident, std::string name,
                                std::string target )
{
    _ident = ident;
    _name = name;
    _target = target;
}

std::string start_location::ident() const
{
    return _ident;
}

std::string start_location::name() const
{
    return _name;
}

std::string start_location::target() const
{
    return _target;
}

location_map::iterator start_location::begin()
{
    return _locations.begin();
}

location_map::iterator start_location::end()
{
    return _locations.end();
}

start_location *start_location::find( const std::string ident )
{
    location_map::iterator found = _locations.find( ident );
    if(found != _locations.end()) {
        return &(found->second);
    } else {
        debugmsg("Tried to get invalid location: %s", ident.c_str());
        static start_location null_location;
        return &null_location;
    }
}

const std::set<std::string> &start_location::flags() const {
    return _flags;
}

void start_location::load_location( JsonObject &jsonobj )
{
    start_location new_location;

    new_location._ident = jsonobj.get_string("ident");
    new_location._name = jsonobj.get_string("name");
    new_location._target = jsonobj.get_string("target");
    new_location._flags = jsonobj.get_tags("flags");

    _locations[new_location._ident] = new_location;
}

// check if tile at p should be boarded with some kind of furniture.
void add_boardable( map &m, const point &p, std::vector<point> &vec )
{
    if( m.has_furn( p.x, p.y ) ) {
        // Don't need to board this up, is already occupied
        return;
    }
    if( m.ter( p.x, p.y ) != t_floor ) {
        // Other terrain (door, wall, ...), not boarded either
        return;
    }
    if( m.is_outside( p.x, p.y ) ) {
        // Don't board up the outside
        return;
    }
    if( std::find( vec.begin(), vec.end(), p ) != vec.end() ) {
        // Already registered to be boarded
        return;
    }
    vec.push_back( p );
}

point get_random_from_vec( std::vector<point> &vec )
{
    if( vec.empty() ) {
        return point( -1, -1 );
    }
    const size_t i = rng( 0, vec.size() - 1 );
    const point p = vec[i];
    vec.erase( vec.begin() + i );
    return p;
}

void board_up( map &m, int sx, int sy, int dx, int dy )
{
    std::vector<point> furnitures1;
    std::vector<point> furnitures2;
    std::vector<point> boardables;
    for( int x = sx; x < sx + dx; x++ ) {
        for( int y = sy; y < sy + dy; y++ ) {
            bool must_board_around = false;
            const ter_id t = m.ter( x, y );
            if( t == t_window_domestic || t == t_window ) {
                // Windows are always to the outside and must be boarded
                must_board_around = true;
                m.ter_set( x, y, t_window_boarded );
            } else if( t == t_door_c || t == t_door_locked || t == t_door_c_peep ) {
                // Only board up doors that lead to the outside
                if( m.is_outside( x + 1, y ) || m.is_outside( x - 1, y ) ||
                    m.is_outside( x, y + 1 ) || m.is_outside( x, y - 1 ) ) {
                    m.ter_set( x, y, t_door_boarded );
                    must_board_around = true;
                } else {
                    // internal doors are opened instead
                    m.ter_set( x, y, t_door_o );
                }
            }
            if( must_board_around ) {
                // Board up the surroundings of the door/window
                add_boardable( m, point( x + 1, y ), boardables );
                add_boardable( m, point( x - 1, y ), boardables );
                add_boardable( m, point( x, y + 1 ), boardables );
                add_boardable( m, point( x, y - 1 ), boardables );
                add_boardable( m, point( x + 1, y + 1 ), boardables );
                add_boardable( m, point( x - 1, y + 1 ), boardables );
                add_boardable( m, point( x + 1, y - 1 ), boardables );
                add_boardable( m, point( x - 1, y - 1 ), boardables );
            }
        }
    }
    // Find all furniture that can be used to board up some place
    for( int x = sx; x < sx + dx; x++ ) {
        for( int y = sy; y < sy + dy; y++ ) {
            if( std::find( boardables.begin(), boardables.end(), point( x, y ) ) != boardables.end() ) {
                continue;
            }
            if( !m.has_furn( x, y ) ) {
                continue;
            }
            if( m.furn_at( x, y ).movecost == 0 ) {
                // Obstacles are better, prefer them
                furnitures1.push_back( point( x, y ) );
            } else {
                furnitures2.push_back( point( x, y ) );
            }
        }
    }
    while( ( !furnitures1.empty() || !furnitures2.empty() ) && !boardables.empty() ) {
        const point fp = furnitures1.empty() ? get_random_from_vec( furnitures2 ) : get_random_from_vec(
                             furnitures1 );
        const point bp = get_random_from_vec( boardables );
        m.furn_set( bp.x, bp.y, m.furn( fp.x, fp.y ) );
        m.furn_set( fp.x, fp.y, f_null );
        m.i_at( bp.x, bp.y ).swap( m.i_at( fp.x, fp.y ) );
    }
}

void start_location::prepare_map( tinymap &m ) const
{
    if( flags().count( "BOARDED" ) > 0 ) {
        m.build_map_cache();
        board_up( m, 0, 0, m.getmapsize() * SEEX, m.getmapsize() * SEEY );
        // The hammer has been used to board up the house
        if( !g->u.has_amount( "hammer", 1 ) ) {
            g->u.i_add( item( "hammer", 0 ) );
        }
    } else {
        m.translate( t_window_domestic, t_curtains );
    }
}

void start_location::setup( overmap *&cur_om, int &levx, int &levy, int &levz ) const
{
    // We start in the (0,0,0) overmap.
    cur_om = &overmap_buffer.get( 0, 0 );
    cur_om->first_house( levx, levy, target() );
    const auto omtstart = tripoint( levx, levy, 0 );

    // Now prepare the initial map (change terrain etc.)
    const point player_location = overmapbuffer::omt_to_sm_copy( omtstart.x, omtstart.y );
    tinymap player_start;
    player_start.load( player_location.x, player_location.y, omtstart.z, false, cur_om );
    prepare_map( player_start );
    player_start.save();

    // Setup game::levx/levy/levz - those are in submap coordinates!
    // And the player is centered in the map
    levx = player_location.x - ( MAPSIZE / 2 );
    levy = player_location.y - ( MAPSIZE / 2 );
    levz = omtstart.z;
}
