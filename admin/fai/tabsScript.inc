<?php

class tabsScript extends tabs
{
  var $base= "";

  function tabsScript($config, $data, $dn,$category)
  {
	tabs::tabs($config, $data, $dn,$category);

	/* Add references/acls/snapshots */
	$this->addSpecialTabs();
  }

  function check($ignore_account= FALSE)
  {
	return (tabs::check(TRUE));
  }
  

  function save($ignore_account= FALSE)
  {
  	$baseobject= $this->by_object['faiScript'];

	$new_dn= 'cn='.$baseobject->cn.",".get_ou('faiscriptou').get_ou('faiou').session::get('CurrentMainBase');

	$faifilter = session::get('faifilter');

	if($faifilter['branch']!="main"){
		$new_dn ='cn='.$baseobject->cn.",".get_ou('faiscriptou').$faifilter['branch'];
	}
	
 	if ($this->dn != $new_dn && $this->dn != "new"){
		
		/* if( new_dn is subtree of this->dn ) */
		$cnt1 = count(split(",",$this->dn));
		$cnt2 = count(split(",",$new_dn));
		if((strstr($new_dn,$this->dn))&&($cnt1<$cnt2)){
			print_red(_("Try to move tree failed. Destination tree is subtree of source tree."));
		}else{
//			$baseobject->recursive_move($this->dn, $new_dn);
		}
	}
	$this->dn= $new_dn;

	tabs::save(TRUE);
  }

}

?>