// ETIL EvolutionEngine — 3D Abstract Visualization
// Dense branching tree with varied lifespans before pruning
//
// Render: povray +W1920 +H1080 +A0.3 +Q9 evolution-abstract.pov

#version 3.7;

global_settings {
    assumed_gamma 1.0
    radiosity {
        pretrace_start 0.08
        pretrace_end 0.01
        count 80
        nearest_count 5
        error_bound 0.5
        recursion_limit 1
        low_error_factor 0.5
        gray_threshold 0.0
        minimum_reuse 0.015
        brightness 0.6
    }
}

// ================================================================
// CAMERA
// ================================================================

camera {
    location <-5, 5, -12>
    look_at <10, 0.5, 3>
    angle 55
    right x * 16/9
}

// ================================================================
// LIGHTING
// ================================================================

light_source {
    <15, 20, -10>
    color rgb <1.0, 0.95, 0.85> * 1.2
    area_light <3, 0, 0>, <0, 0, 3>, 5, 5
    adaptive 1
    jitter
}

light_source {
    <-15, 12, -5>
    color rgb <0.4, 0.6, 0.9> * 0.5
}

light_source {
    <10, 5, 25>
    color rgb <1.0, 0.5, 0.2> * 0.35
}

// ================================================================
// BACKGROUND
// ================================================================

sky_sphere {
    pigment {
        gradient y
        color_map {
            [0.0 color rgb <0.02, 0.03, 0.06>]
            [0.3 color rgb <0.04, 0.05, 0.10>]
            [1.0 color rgb <0.06, 0.07, 0.14>]
        }
        scale 2
        translate -1
    }
}

// ================================================================
// GROUND PLANE — Reduced reflection
// ================================================================

plane {
    y, -0.8
    pigment { color rgb <0.03, 0.04, 0.07> }
    finish {
        reflection { 0.12 }
        specular 0.1
        roughness 0.05
        ambient 0.02
    }
}

// ================================================================
// MATERIALS
// ================================================================

#macro NodeMaterial(BaseColor, Glow)
    texture {
        pigment { color BaseColor * 1.4 transmit 0.05 }
        finish {
            specular 0.9
            roughness 0.015
            reflection { 0.12 }
            ambient 0.15
            diffuse 0.8
        }
    }
    #if (Glow > 0)
        interior { ior 1.3 }
    #end
#end

#macro DeadMaterial()
    texture {
        pigment { color rgb <0.12, 0.14, 0.18> transmit 0.4 }
        finish {
            specular 0.1
            roughness 0.3
            ambient 0.01
            diffuse 0.3
        }
    }
#end

#macro ConnectionMaterial(Col, Alpha)
    texture {
        pigment { color Col * 1.8 transmit (Alpha * 0.5) }
        finish {
            specular 0.4
            roughness 0.08
            ambient 0.25
            diffuse 0.7
        }
    }
#end

// ================================================================
// MACROS
// ================================================================

#macro EvNode(Pos, Radius, BaseColor, Glow)
    sphere {
        Pos, Radius
        NodeMaterial(BaseColor, Glow)
    }
    #if (Glow > 0)
        sphere {
            Pos, Radius * 1.8
            pigment { color BaseColor * 1.5 transmit 0.82 }
            finish { ambient 1.0 diffuse 0.0 }
            no_shadow
        }
    #end
#end

#macro DeadNode(Pos, Radius)
    sphere {
        Pos, Radius
        DeadMaterial()
    }
    torus {
        Radius * 1.1, Radius * 0.06
        rotate x * 90
        translate Pos
        pigment { color rgb <0.9, 0.2, 0.15> transmit 0.5 }
        finish { ambient 0.3 diffuse 0.5 }
        no_shadow
    }
#end

#macro Connection(From, To, Radius, Col, Alpha)
    cylinder {
        From, To, Radius
        ConnectionMaterial(Col, Alpha)
        no_shadow
    }
#end

// ================================================================
// COLOR PALETTE — interpolate by generation
// ================================================================

#declare ColG0  = rgb <0.31, 0.76, 0.97>;  // bright cyan
#declare ColG1  = rgb <0.15, 0.78, 0.85>;  // teal
#declare ColG2  = rgb <0.40, 0.73, 0.42>;  // green
#declare ColG3  = rgb <1.0, 0.72, 0.30>;   // amber
#declare ColG4  = rgb <1.0, 0.56, 0.0>;    // orange
#declare ColG5  = rgb <1.0, 0.44, 0.0>;    // deep orange
#declare ColG6  = rgb <0.90, 0.32, 0.0>;   // red-orange
#declare ColG7  = rgb <0.75, 0.21, 0.05>;  // ember
#declare ColG8  = rgb <0.53, 0.12, 0.02>;  // dark ember

// Z-spread per generation (depth into scene)
#declare Zstep = 1.8;

// ================================================================
// GENERATION 0 — The Ancestor
// ================================================================

#declare G0 = <0, 0.5, 0>;
EvNode(G0, 0.65, ColG0, 1)

// ================================================================
// GENERATION 1 — 5 children, 3 survive
// ================================================================

#declare G1a = <2.5, 0.5, 1.2>;    // survives — strong
#declare G1b = <2.5, 0.5, 2.8>;    // survives — medium
#declare G1c = <2.3, 0.5, -0.8>;   // survives — weak, dies at G3
#declare G1d = <2.0, 0.5, -2.0>;   // dies immediately
#declare G1e = <2.2, 0.5, 3.8>;    // dies immediately

EvNode(G1a, 0.48, ColG1, 1)
EvNode(G1b, 0.45, ColG1, 1)
EvNode(G1c, 0.42, ColG1, 0)
DeadNode(G1d, 0.32)
DeadNode(G1e, 0.32)

Connection(G0, G1a, 0.06, ColG0, 0.15)
Connection(G0, G1b, 0.06, ColG0, 0.15)
Connection(G0, G1c, 0.045, ColG0, 0.25)
Connection(G0, G1d, 0.025, ColG0, 0.6)
Connection(G0, G1e, 0.025, ColG0, 0.6)

// ================================================================
// GENERATION 2 — Heavy branching
// ================================================================

// From G1a (strong — 4 children, 3 survive)
#declare G2a1 = <5.0, 0.5, 1.5>;
#declare G2a2 = <5.0, 0.5, 3.2>;
#declare G2a3 = <4.8, 0.5, 0.0>;
#declare G2a4 = <4.6, 0.5, -1.2>;   // dies

EvNode(G2a1, 0.42, ColG2, 1)
EvNode(G2a2, 0.40, ColG2, 0)
EvNode(G2a3, 0.38, ColG2, 0)
DeadNode(G2a4, 0.26)

Connection(G1a, G2a1, 0.055, ColG1, 0.15)
Connection(G1a, G2a2, 0.05, ColG1, 0.2)
Connection(G1a, G2a3, 0.045, ColG1, 0.25)
Connection(G1a, G2a4, 0.02, ColG1, 0.6)

// From G1b (medium — 3 children, 1 survives)
#declare G2b1 = <5.2, 0.5, 4.2>;
#declare G2b2 = <4.8, 0.5, 5.5>;    // dies
#declare G2b3 = <5.0, 0.5, 5.0>;    // dies

EvNode(G2b1, 0.38, rgb <0.30, 0.71, 0.67>, 0)
DeadNode(G2b2, 0.26)
DeadNode(G2b3, 0.26)

Connection(G1b, G2b1, 0.045, ColG1, 0.2)
Connection(G1b, G2b2, 0.02, ColG1, 0.6)
Connection(G1b, G2b3, 0.02, ColG1, 0.6)

// From G1c (weak — 2 children, both die at G3)
#declare G2c1 = <4.5, 0.5, -1.5>;
#declare G2c2 = <4.8, 0.5, -2.2>;

EvNode(G2c1, 0.32, rgb <0.25, 0.68, 0.72>, 0)
EvNode(G2c2, 0.30, rgb <0.28, 0.65, 0.68>, 0)

Connection(G1c, G2c1, 0.035, ColG1, 0.3)
Connection(G1c, G2c2, 0.03, ColG1, 0.35)

// ================================================================
// GENERATION 3 — Convergence begins
// ================================================================

// From G2a1 (star lineage — 3 children, 2 survive)
#declare G3a1 = <7.5, 0.5, 2.0>;    // fittest path
#declare G3a2 = <7.3, 0.5, 3.5>;
#declare G3a3 = <7.0, 0.5, 0.5>;    // dies

EvNode(G3a1, 0.40, ColG3, 1)
EvNode(G3a2, 0.35, ColG3, 0)
DeadNode(G3a3, 0.24)

Connection(G2a1, G3a1, 0.045, ColG2, 0.15)
Connection(G2a1, G3a2, 0.04, ColG2, 0.25)
Connection(G2a1, G3a3, 0.018, ColG2, 0.6)

// From G2a2 (secondary — 2 children, 1 survives)
#declare G3b1 = <7.5, 0.5, 4.5>;
#declare G3b2 = <7.2, 0.5, 5.5>;    // dies

EvNode(G3b1, 0.32, ColG3, 0)
DeadNode(G3b2, 0.22)

Connection(G2a2, G3b1, 0.035, ColG2, 0.25)
Connection(G2a2, G3b2, 0.015, ColG2, 0.65)

// From G2a3 (tertiary — 2 children, 1 survives, dies at G5)
#declare G3c1 = <7.2, 0.5, -1.0>;
#declare G3c2 = <7.0, 0.5, -2.0>;   // dies

EvNode(G3c1, 0.28, ColG3, 0)
DeadNode(G3c2, 0.20)

Connection(G2a3, G3c1, 0.03, ColG2, 0.3)
Connection(G2a3, G3c2, 0.015, ColG2, 0.65)

// From G2b1 (branch — survives 2 more gens then dies at G5)
#declare G3d1 = <7.8, 0.5, 5.5>;
#declare G3d2 = <7.5, 0.5, 6.5>;    // dies

EvNode(G3d1, 0.28, rgb <0.80, 0.60, 0.25>, 0)
DeadNode(G3d2, 0.20)

Connection(G2b1, G3d1, 0.03, rgb <0.30, 0.71, 0.67>, 0.3)
Connection(G2b1, G3d2, 0.015, rgb <0.30, 0.71, 0.67>, 0.65)

// G2c1 and G2c2 — both die here (delayed pruning)
#declare G3e1 = <7.0, 0.5, -2.5>;   // dies
#declare G3e2 = <7.2, 0.5, -3.2>;   // dies

DeadNode(G3e1, 0.22)
DeadNode(G3e2, 0.20)

Connection(G2c1, G3e1, 0.015, rgb <0.25, 0.68, 0.72>, 0.55)
Connection(G2c2, G3e2, 0.012, rgb <0.28, 0.65, 0.68>, 0.6)

// ================================================================
// GENERATION 4 — Fittest emerge (golden)
// ================================================================

// From G3a1 (star — 3 children, 2 survive)
#declare G4a1 = <10.0, 0.5, 2.5>;   // primary fittest
#declare G4a2 = <10.0, 0.5, 4.0>;
#declare G4a3 = <9.5, 0.5, 0.8>;    // dies

EvNode(G4a1, 0.38, ColG4, 1)
EvNode(G4a2, 0.30, ColG4, 0)
DeadNode(G4a3, 0.22)

Connection(G3a1, G4a1, 0.04, ColG3, 0.15)
Connection(G3a1, G4a2, 0.035, ColG3, 0.25)
Connection(G3a1, G4a3, 0.015, ColG3, 0.6)

// From G3a2 (secondary — 1 child survives)
#declare G4b1 = <10.2, 0.5, 5.2>;

EvNode(G4b1, 0.28, ColG4, 0)

Connection(G3a2, G4b1, 0.03, ColG3, 0.3)

// From G3b1 (tertiary — 2 children, 1 survives, dies at G6)
#declare G4c1 = <10.0, 0.5, 5.8>;
#declare G4c2 = <9.8, 0.5, 6.5>;    // dies

EvNode(G4c1, 0.25, ColG4, 0)
DeadNode(G4c2, 0.18)

Connection(G3b1, G4c1, 0.025, ColG3, 0.3)
Connection(G3b1, G4c2, 0.012, ColG3, 0.65)

// From G3c1 (side branch — 1 child, dies at G5)
#declare G4d1 = <9.8, 0.5, -0.5>;

EvNode(G4d1, 0.22, rgb <0.90, 0.60, 0.20>, 0)

Connection(G3c1, G4d1, 0.022, ColG3, 0.35)

// From G3d1 (side branch — 1 child, dies at G5)
#declare G4e1 = <10.2, 0.5, 6.8>;

EvNode(G4e1, 0.22, rgb <0.85, 0.55, 0.18>, 0)

Connection(G3d1, G4e1, 0.022, rgb <0.80, 0.60, 0.25>, 0.35)

// ================================================================
// GENERATION 5 — Deep convergence
// ================================================================

// From G4a1 (primary — 2 children survive)
#declare G5a = <12.5, 0.5, 3.0>;
#declare G5b = <12.5, 0.5, 4.5>;

EvNode(G5a, 0.32, ColG5, 1)
EvNode(G5b, 0.25, ColG5, 0)

Connection(G4a1, G5a, 0.038, ColG4, 0.2)
Connection(G4a1, G5b, 0.028, ColG4, 0.35)

// From G4a2 (1 child survives)
#declare G5c = <12.8, 0.5, 5.5>;

EvNode(G5c, 0.22, ColG5, 0)

Connection(G4a2, G5c, 0.025, ColG4, 0.3)

// G4b1 branch — 1 child, dies at G6
#declare G5d = <12.5, 0.5, 6.5>;

EvNode(G5d, 0.20, rgb <0.95, 0.42, 0.05>, 0)

Connection(G4b1, G5d, 0.02, ColG4, 0.4)

// G4c1 branch — dies here
#declare G5e_dead = <12.0, 0.5, 7.0>;
DeadNode(G5e_dead, 0.18)
Connection(G4c1, G5e_dead, 0.012, ColG4, 0.6)

// G4d1 branch — dies here
#declare G5f_dead = <12.2, 0.5, -0.8>;
DeadNode(G5f_dead, 0.16)
Connection(G4d1, G5f_dead, 0.012, rgb <0.90, 0.60, 0.20>, 0.6)

// G4e1 branch — dies here
#declare G5g_dead = <12.5, 0.5, 7.8>;
DeadNode(G5g_dead, 0.16)
Connection(G4e1, G5g_dead, 0.012, rgb <0.85, 0.55, 0.18>, 0.6)

// ================================================================
// GENERATION 6 — Narrowing
// ================================================================

#declare G6a = <14.8, 0.5, 3.5>;
#declare G6b = <14.5, 0.5, 5.0>;
#declare G6c = <14.8, 0.5, 6.0>;    // dies

EvNode(G6a, 0.28, ColG6, 1)
EvNode(G6b, 0.20, ColG6, 0)
DeadNode(G6c, 0.16)

Connection(G5a, G6a, 0.032, ColG5, 0.25)
Connection(G5a, G6b, 0.022, ColG5, 0.35)
Connection(G5b, G6c, 0.012, ColG5, 0.6)

// G5c branch — 1 more gen then dies
#declare G6d = <15.0, 0.5, 6.5>;
EvNode(G6d, 0.18, ColG6, 0)
Connection(G5c, G6d, 0.018, ColG5, 0.4)

// G5d — dies here
#declare G6e_dead = <14.5, 0.5, 7.2>;
DeadNode(G6e_dead, 0.14)
Connection(G5d, G6e_dead, 0.01, rgb <0.95, 0.42, 0.05>, 0.65)

// ================================================================
// GENERATION 7 — Embers
// ================================================================

#declare G7a = <16.8, 0.5, 4.0>;
#declare G7b = <16.5, 0.5, 5.5>;

EvNode(G7a, 0.22, ColG7, 1)
EvNode(G7b, 0.15, ColG7, 0)

Connection(G6a, G7a, 0.025, ColG6, 0.3)
Connection(G6a, G7b, 0.015, ColG6, 0.5)

// G6b — dies
#declare G7c_dead = <16.5, 0.5, 6.5>;
DeadNode(G7c_dead, 0.12)
Connection(G6b, G7c_dead, 0.008, ColG6, 0.65)

// G6d — dies
#declare G7d_dead = <16.8, 0.5, 7.0>;
DeadNode(G7d_dead, 0.12)
Connection(G6d, G7d_dead, 0.008, ColG6, 0.65)

// ================================================================
// GENERATION 8 — Vanishing
// ================================================================

#declare G8a = <18.5, 0.5, 4.5>;
#declare G8b = <18.2, 0.5, 5.8>;

EvNode(G8a, 0.18, ColG8, 1)
EvNode(G8b, 0.12, ColG8, 0)

Connection(G7a, G8a, 0.02, ColG7, 0.35)
Connection(G7a, G8b, 0.012, ColG7, 0.5)

// ================================================================
// GENERATION 9 — Sparks
// ================================================================

#declare G9a = <20.0, 0.5, 5.0>;
EvNode(G9a, 0.12, rgb <0.30, 0.06, 0.0>, 1)
Connection(G8a, G9a, 0.015, ColG8, 0.45)

#declare G9b = <21.0, 0.5, 5.5>;
sphere {
    G9b, 0.07
    pigment { color rgb <0.20, 0.04, 0.0> transmit 0.3 }
    finish { ambient 0.3 diffuse 0.4 specular 0.5 }
}
Connection(G9a, G9b, 0.01, rgb <0.30, 0.06, 0.0>, 0.55)

// ================================================================
// FLOATING PARTICLES
// ================================================================

#macro Particle(Pos, Size, Col)
    sphere {
        Pos, Size
        pigment { color Col * 1.5 transmit 0.7 }
        finish { ambient 0.9 diffuse 0.0 }
        no_shadow
    }
#end

Particle(<1.5, 1.8, 0.5>, 0.06, ColG0)
Particle(<3.5, 1.5, 1.0>, 0.05, ColG1)
Particle(<4.0, 1.2, -3.0>, 0.04, ColG1)
Particle(<6.0, 1.8, -1.5>, 0.05, ColG2)
Particle(<6.5, 1.3, 7.0>, 0.04, ColG2)
Particle(<8.5, 1.6, -1.0>, 0.04, ColG3)
Particle(<9.0, 1.2, 7.5>, 0.03, ColG3)
Particle(<11.0, 1.5, 1.0>, 0.04, ColG4)
Particle(<13.0, 1.4, 0.5>, 0.03, ColG5)
Particle(<15.0, 1.3, 1.0>, 0.03, ColG6)
Particle(<17.0, 1.2, 2.0>, 0.02, ColG7)
Particle(<19.0, 1.1, 3.0>, 0.015, ColG8)

Particle(<2.0, 1.0, 4.5>, 0.05, rgb <0.15, 0.40, 0.50>)
Particle(<5.5, 1.2, 6.5>, 0.04, rgb <0.20, 0.36, 0.21>)
Particle(<8.0, 1.0, 7.0>, 0.03, rgb <0.50, 0.33, 0.08>)
Particle(<11.5, 1.1, 8.0>, 0.025, rgb <0.50, 0.28, 0.0>)
Particle(<14.0, 1.0, 8.5>, 0.02, rgb <0.45, 0.16, 0.0>)
Particle(<3.0, 1.3, 4.5>, 0.035, ColG1)
Particle(<7.0, 1.2, -0.5>, 0.03, ColG2)
Particle(<10.5, 1.4, -0.5>, 0.025, ColG3)
