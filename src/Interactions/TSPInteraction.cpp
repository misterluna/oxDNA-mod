#include <fstream>

#include "TSPInteraction.h"

template<typename number>
TSPInteraction<number>::TSPInteraction() : BaseInteraction<number, TSPInteraction<number> >() {
	this->_int_map[BONDED] = &TSPInteraction<number>::pair_interaction_bonded;
	this->_int_map[NONBONDED] = &TSPInteraction<number>::pair_interaction_nonbonded;
}

template<typename number>
TSPInteraction<number>::~TSPInteraction() {
	delete[] _alpha;
	delete[] _N_arms;
	delete[] _N_monomer_per_arm;
}

template<typename number>
void TSPInteraction<number>::get_settings(input_file &inp) {
	IBaseInteraction<number>::get_settings(inp);

	double tmp;
	getInputDouble(&inp, "TSP_rfene", &tmp, 1);
	_rfene = tmp;

	for(int i = 0; i < 3; i++) {
		char key[256];
		sprintf(key, "TSP_sigma[%d]", i);
		getInputDouble(&inp, key, &tmp, 1);
		_TSP_sigma[i] = tmp;

		sprintf(key, "TSP_rcut[%d]", i);
		getInputDouble(&inp, key, &tmp, 1);
		_TSP_rcut[i] = tmp;

		sprintf(key, "TSP_epsilon[%d]", i);
		getInputDouble(&inp, key, &tmp, 1);
		_TSP_epsilon[i] = tmp;

		sprintf(key, "TSP_attractive[%d]", i);
		getInputDouble(&inp, key, &tmp, 1);
		_TSP_attractive[i] = tmp;

		sprintf(key, "TSP_n[%d]", i);
		getInputInt(&inp, key, _TSP_n + i, 1);
	}
}

template<typename number>
void TSPInteraction<number>::init() {
	_sqr_rfene = SQR(_rfene);
	_rfene_anchor = _rfene * 3;
	_sqr_rfene_anchor = SQR(_rfene_anchor);

	this->_rcut = 0.;
	for(int i = 0; i < 3; i++) {
		if(_TSP_rcut[i] > this->_rcut) this->_rcut = _TSP_rcut[i];
		if(_TSP_n[i] % 2 != 0) throw oxDNAException("TSP_n must be a multiple of 2");

		_TSP_sqr_rcut[i] = SQR(_TSP_rcut[i]);
		_TSP_sqr_sigma[i] = SQR(_TSP_sigma[i]);

		_TSP_cut_energy[i] = 4 * _TSP_epsilon[i] * (pow(_TSP_sigma[i] / _TSP_rcut[i], 2*_TSP_n[i]) - _TSP_attractive[i] * pow(_TSP_sigma[i] / _TSP_rcut[i], _TSP_n[i]));
	}
	this->_sqr_rcut = SQR(this->_rcut);
}

template<typename number>
number TSPInteraction<number>::_fene(BaseParticle<number> *p, BaseParticle<number> *q, LR_vector<number> *r, bool update_forces) {
	bool anchor = ((TSPParticle<number> *)p)->is_anchor() || ((TSPParticle<number> *)q)->is_anchor();

	number sqr_r = r->norm();
	number sqr_rfene = (anchor) ? _sqr_rfene_anchor : _sqr_rfene;

	if(sqr_r > sqr_rfene) {
		if(update_forces) throw oxDNAException("The distance between particles %d and %d (%lf) exceeds the FENE distance (%lf)\n", p->index, q->index, sqrt(sqr_r), sqrt(_sqr_rfene));
		else return 1e10;
	}

	number energy = -15*_TSP_epsilon[0] * sqr_rfene * log(1 - sqr_r/sqr_rfene);

	if(update_forces) {
		// this number is the module of the force over r, so we don't have to divide the distance
		// vector by its module
		number force_mod = -30*_TSP_epsilon[0] * sqr_rfene / (sqr_rfene - sqr_r);
		p->force -= *r * force_mod;
		q->force += *r * force_mod;
	}

	return energy;
}

template<typename number>
number TSPInteraction<number>::_nonbonded(BaseParticle<number> *p, BaseParticle<number> *q, LR_vector<number> *r, bool update_forces) {
	int int_type = q->type + p->type;
	if(_TSP_epsilon[int_type] == (number) 0.) return (number) 0.;

	number sqr_r = r->norm();
	if(sqr_r > _TSP_sqr_rcut[int_type]) return (number) 0.;

	number part = pow(_TSP_sqr_sigma[int_type] / sqr_r, _TSP_n[int_type] / 2);
	number energy = -_TSP_cut_energy[int_type];

	energy += 4 * _TSP_epsilon[int_type] * part * (part - (number) _TSP_attractive[int_type]);

	if(update_forces) {
		// this number is the module of the force over r, so we don't have to divide the distance
		// vector for its module
		number force_mod = 4 * _TSP_n[int_type] * _TSP_epsilon[int_type] * part * (2*part - (number) _TSP_attractive[int_type]) / sqr_r;
		p->force -= *r * force_mod;
		q->force += *r * force_mod;
	}

	return energy;
}

template<typename number>
number TSPInteraction<number>::pair_interaction(BaseParticle<number> *p, BaseParticle<number> *q, LR_vector<number> *r, bool update_forces) {
	if(p->is_bonded(q)) return pair_interaction_bonded(p, q, r, update_forces);
	else return pair_interaction_nonbonded(p, q, r, update_forces);
}

template<typename number>
number TSPInteraction<number>::pair_interaction_bonded(BaseParticle<number> *p, BaseParticle<number> *q, LR_vector<number> *r, bool update_forces) {
	number energy = (number) 0.f;

	// if q == P_VIRTUAL we have to compute the bonded interactions acting between p and all its bonded neighbours
	if(q == P_VIRTUAL) {
		TSPParticle<number> *TSPp = (TSPParticle<number> *) p;
		for(typename set<TSPParticle<number> *>::iterator it = TSPp->bonded_neighs.begin(); it != TSPp->bonded_neighs.end(); it++) {
			energy += pair_interaction_bonded(p, *it, r, update_forces);
		}
	}
	else if(p->is_bonded(q)){
		LR_vector<number> computed_r(0, 0, 0);
		if(r == NULL) {
			if (q != P_VIRTUAL && p != P_VIRTUAL) {
				computed_r = q->pos - p->pos;
				r = &computed_r;
			}
		}

		if(p->index < q->index && update_forces) return energy;
		energy = _fene(p, q, r, update_forces);
		energy += _nonbonded(p, q, r, update_forces);
	}
	return energy;
}

template<typename number>
number TSPInteraction<number>::pair_interaction_nonbonded(BaseParticle<number> *p, BaseParticle<number> *q, LR_vector<number> *r, bool update_forces) {
	if(p->is_bonded(q)) return (number) 0.f;

	LR_vector<number> computed_r(0, 0, 0);
	if(r == NULL) {
		computed_r = q->pos.minimum_image(p->pos, this->_box_side);
		r = &computed_r;
	}

	if(r->norm() >= this->_sqr_rcut) return (number) 0;

	return _nonbonded(p, q, r, update_forces);
}

template<typename number>
void TSPInteraction<number>::check_input_sanity(BaseParticle<number> **particles, int N) {
	for(int i = 0; i < N; i++) {	
		TSPParticle<number> *p = (TSPParticle<number> *)particles[i];
		if(p->n3 != P_VIRTUAL && p->n3->index >= N) throw oxDNAException("Wrong topology for particle %d (n3 neighbor is %d, should be < N = %d)", i, p->n3->index, N);
		if(p->n5 != P_VIRTUAL && p->n5->index >= N) throw oxDNAException("Wrong topology for particle %d (n5 neighbor is %d, should be < N = %d)", i, p->n5->index, N);

		// check that the distance between bonded neighbor doesn't exceed a reasonable threshold
		number mind = _TSP_sigma[0]*0.5;
		number maxd = _rfene;
		if(p->n3 != P_VIRTUAL && p->n3->n5 != P_VIRTUAL) {
			BaseParticle<number> *q = p->n3;
			LR_vector<number> rv = p->pos - q->pos;
			number r = sqrt(rv*rv);
			if(r > maxd || r < mind)
				throw oxDNAException("Distance between bonded neighbors %d and %d exceeds acceptable values (d = %lf)", i, p->n3->index, r);
		}

		if(p->n5 != P_VIRTUAL && p->n5->n3 != P_VIRTUAL) {
			BaseParticle<number> *q = p->n5;
			LR_vector<number> rv = p->pos - q->pos;
			number r = sqrt(rv*rv);
			if(r > maxd || r < mind)
				throw oxDNAException("Distance between bonded neighbors %d and %d exceeds acceptable values (d = %lf)", i, p->n5->index, r);
		}
	}
}

template<typename number>
void TSPInteraction<number>::allocate_particles(BaseParticle<number> **particles, int N) {
	for(int i = 0; i < N; i++) particles[i] = new TSPParticle<number>();
}

template<typename number>
int TSPInteraction<number>::get_N_from_topology() {
	char line[512];
	std::ifstream topology;
	topology.open(this->_topology_filename, ios::in);
	if(!topology.good()) throw oxDNAException("Can't read topology file '%s'. Aborting", this->_topology_filename);

	topology.getline(line, 512);

	int my_N_stars;
	sscanf(line, "%d\n", &my_N_stars);

	int N_arms, N_monomer_per_arm;
	// because we have at least a particle per star (the anchor)
	int N_from_topology = my_N_stars;
	for(int i = 0; i < my_N_stars; i++) {
		if(!topology.good()) throw oxDNAException("Not enough stars found in the topology file. There are only %d lines, there should be %d, aborting", i, my_N_stars);
		topology.getline(line, 512);
		if(sscanf(line, "%d %d %*f\n", &N_arms, &N_monomer_per_arm) != 2) throw new oxDNAException("The topology file does not contain any info on star n.%d\n", i);

		N_from_topology += N_monomer_per_arm * N_arms;
	}

	return N_from_topology;
}

template<typename number>
void TSPInteraction<number>::read_topology(int N_from_conf, int *N_stars, BaseParticle<number> **particles) {
	IBaseInteraction<number>::read_topology(N_from_conf, N_stars, particles);
	int my_N_stars;
	char line[512];
	std::ifstream topology;
	topology.open(this->_topology_filename, ios::in);

	if(!topology.good()) throw oxDNAException("Can't read topology file '%s'. Aborting", this->_topology_filename);

	topology.getline(line, 512);
	sscanf(line, "%d\n", &my_N_stars);

	_N_arms = new int[my_N_stars];
	_N_monomer_per_arm = new int[my_N_stars];
	_alpha = new number[my_N_stars];
	// because we have at least _N_stars anchors
	int N_from_topology = my_N_stars;
	for(int i = 0; i < my_N_stars; i++) {
		if(!topology.good()) throw oxDNAException("Not enough stars found in the topology file. There are only %d lines, there should be %d, aborting", i, my_N_stars);
		topology.getline(line, 512);
		float tmp;
		sscanf(line, "%d %d %f\n", _N_arms + i, _N_monomer_per_arm + i, &tmp);
		_alpha[i] = tmp;

		N_from_topology += _N_monomer_per_arm[i] * _N_arms[i];
	}

	// construct the topology, i.e. assign the right FENE neighbours to all the particles
	int p_ind = 0;
	for(int ns = 0; ns < my_N_stars; ns++) {
		TSPParticle<number> *anchor = (TSPParticle<number> *) particles[p_ind];
		anchor->flag_as_anchor();
		// this is an anchor: it has n_arms FENE neighbours since it is attached to each arm
		anchor->btype = anchor->type = P_A;
		anchor->n3 = anchor->n5 = P_VIRTUAL;
		anchor->strand_id = ns;

		_anchors.push_back(anchor);

		p_ind++;
		for(int na = 0; na < _N_arms[ns]; na++) {
			int attractive_from = (int) round(_N_monomer_per_arm[ns] * (1. - _alpha[ns]));
			for(int nm = 0; nm < _N_monomer_per_arm[ns]; nm++) {
				int type = (nm > attractive_from) ? P_B : P_A;
				TSPParticle<number> *p = (TSPParticle<number> *) particles[p_ind];
				p->type = p->btype = type;
				p->strand_id = ns;
				p->n3 = p->n5 = P_VIRTUAL;
				// if it's the first monomer of the arm then add also the anchor
				// as a bonded neighbour
				if(nm == 0) {
					p->add_bonded_neigh(anchor);
					p->n3 = anchor;
				}
				else {
					TSPParticle<number> *p_n = (TSPParticle<number> *) particles[p_ind-1];
					p->n3 = p_n;
					p_n->n5 = p;
					p->add_bonded_neigh(p_n);
				}

				p_ind++;
			}
		}
	}

	if(N_from_topology < N_from_conf) throw oxDNAException ("Not enough particles found in the topology file (should be %d). Aborting", N_from_conf);

	topology.close();

	*N_stars = my_N_stars;
	_N_stars = my_N_stars;
}

template<typename number>
void TSPInteraction<number>::_insert_in_cell_set_orientation(BaseParticle<number> *p, number box_side) {
	int cell_index = (int) ((p->pos.x / box_side - floor(p->pos.x / box_side)) * (1.f - FLT_EPSILON) * this->_cells_N_side);
	cell_index += this->_cells_N_side * ((int) ((p->pos.y / box_side - floor(p->pos.y / box_side)) * (1.f - FLT_EPSILON) * this->_cells_N_side));
	cell_index += this->_cells_N_side * this->_cells_N_side * ((int) ((p->pos.z / box_side - floor(p->pos.z / box_side)) * (1.f - FLT_EPSILON) * this->_cells_N_side));

	int old_head = this->_cells_head[cell_index];
	this->_cells_head[cell_index] = p->index;
	this->_cells_index[p->index] = cell_index;
	this->_cells_next[p->index] = old_head;

	p->orientation.v1 = Utils::get_random_vector<number>();
	p->orientation.v2 = Utils::get_random_vector<number>();
	p->orientation.v3 = Utils::get_random_vector<number>();
	Utils::orthonormalize_matrix<number>(p->orientation);
}

template<typename number>
bool TSPInteraction<number>::_insert_anchor(BaseParticle<number> **particles, BaseParticle<number> *p, number box_side) {
	int i = 0;
	bool inserted = false;
	do {
		p->pos = LR_vector<number>(drand48()*box_side, drand48()*box_side, drand48()*box_side);
		inserted = !_does_overlap(particles, p, box_side);
		i++;
	} while(!inserted && i < MAX_INSERTION_TRIES);

	_insert_in_cell_set_orientation(p, box_side);

	return (i != MAX_INSERTION_TRIES);
}

template<typename number>
bool TSPInteraction<number>::_does_overlap(BaseParticle<number> **particles, BaseParticle<number> *p, number box_side) {
	int cell_index = (int) ((p->pos.x / box_side - floor(p->pos.x / box_side)) * (1.f - FLT_EPSILON) * this->_cells_N_side);
	cell_index += this->_cells_N_side * ((int) ((p->pos.y / box_side - floor(p->pos.y / box_side)) * (1.f - FLT_EPSILON) * this->_cells_N_side));
	cell_index += this->_cells_N_side * this->_cells_N_side * ((int) ((p->pos.z / box_side - floor(p->pos.z / box_side)) * (1.f - FLT_EPSILON) * this->_cells_N_side));

	int ind[3], loop_ind[3];
	ind[0] = cell_index % this->_cells_N_side;
	ind[1] = (cell_index / this->_cells_N_side) % this->_cells_N_side;
	ind[2] = cell_index / (this->_cells_N_side * this->_cells_N_side);
	for(int j = -1; j < 2; j++) {
		loop_ind[0] = (ind[0] + j + this->_cells_N_side) % this->_cells_N_side;
		for(int k = -1; k < 2; k++) {
			loop_ind[1] = (ind[1] + k + this->_cells_N_side) % this->_cells_N_side;
			for(int l = -1; l < 2; l++) {
				loop_ind[2] = (ind[2] + l + this->_cells_N_side) % this->_cells_N_side;
				int loop_index = (loop_ind[2] * this->_cells_N_side + loop_ind[1]) * this->_cells_N_side + loop_ind[0];

				int j = this->_cells_head[loop_index];
				while (j != P_INVALID) {
					BaseParticle<number> *q = particles[j];
					if(p->pos.minimum_image(q->pos, box_side).norm() < SQR(this->_sqr_rcut)) return true;
					j = this->_cells_next[q->index];
				}
			}
		}
	}

	return false;
}

template<typename number>
void TSPInteraction<number>::generate_random_configuration(BaseParticle<number> **particles, int N, number box_side) {
	number old_rcut = this->_rcut;
	this->_rcut = _TSP_sigma[0];

	this->_create_cells(particles, N, box_side);
	for(int i = 0; i < N; i++) this->_cells_next[i] = P_INVALID;
	this->_cells_head[0] = P_INVALID;

	int i = 0;
	for(int ns = 0; ns < _N_stars; ns++) {
		BaseParticle<number> *p = particles[i];
		p->index = i;
		if(!_insert_anchor(particles, p, box_side)) throw oxDNAException("Can't insert particle number %d", i);
		i++;

		LR_vector<number> anchor_pos = p->pos;

		for(int na = 0; na < _N_arms[ns]; na++) {
			LR_vector<number> dir = _TSP_sigma[0]*Utils::get_random_vector<number>();
			number arm_dist = drand48()*3 + 1.;
			for(int nm = 0; nm < _N_monomer_per_arm[ns]; nm++) {
				BaseParticle<number> *p = particles[i];
				p->pos = anchor_pos + dir*(nm+arm_dist);
				p->index = i;
				if(nm == 0) {
					if(_does_overlap(particles, p, box_side)) {
						na--;
						break;
					}
				}

				_insert_in_cell_set_orientation(p, box_side);

				i++;
			}
		}
	}

	this->_rcut = old_rcut;
	this->_delete_cell_neighs();
}

template class TSPInteraction<float>;
template class TSPInteraction<double>;

